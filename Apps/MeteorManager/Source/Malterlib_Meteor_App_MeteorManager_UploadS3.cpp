// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Meteor_App_MeteorManager_Server.h"

#include <Mib/File/ExeFS>
#include <Mib/File/VirtualFS>
#include <Mib/File/VirtualFSs/MalterlibFS>
#include <Mib/Encoding/JSONShortcuts>

namespace NMib::NMeteor::NMeteorManager
{
	TCContinuation<void> CMeteorManagerActor::fp_SetupPrerequisites_UploadS3()
	{
		TCContinuation<void> Continuation;

		CStr ProgramDirectory = CFile::fs_GetProgramDirectory();

		TCSet<CStr> ChecksumFiles;

		CDirectoryManifestConfig ManifestConfig;
		ManifestConfig.m_IncludeWildcards.f_Clear();
		ManifestConfig.m_Root = ProgramDirectory;
		bool bAllowRobots = false;

		for (auto &Package : mp_Options.m_Packages)
		{
			if (!Package.m_bUploadS3)
				continue;

			CStr ServerName = fp_GetPackageHostname(Package.f_GetName(), EHostnamePrefix_None);
			bool bIsMainServer = ServerName == mp_Domain;

			CStr StaticPath;
			if (!bIsMainServer)
				StaticPath = Package.m_StaticPath;
			else if (!bIsMainServer && Package.m_StaticPath.f_IsEmpty())
				continue;

			ManifestConfig.m_IncludeWildcards[Package.f_GetName() / "^*"_f] = StaticPath;

			CStr ChecksumFile = ProgramDirectory / ("{}.tar.gz"_f << Package.f_GetName());
			ChecksumFiles[ChecksumFile];

			if (bIsMainServer && Package.m_bAllowRobots && mp_bAllowRobots)
				bAllowRobots = true;
		}

		if (ManifestConfig.m_IncludeWildcards.f_IsEmpty())
		{
			Continuation.f_SetResult();
			return fg_Explicit();
		}

		DLog(Info, "Uploading static files to S3");

		CAwsS3Actor::CCredentials AWSCredentials;

		AWSCredentials.m_Region = fp_GetConfigValue("S3Region", "").f_String();
		AWSCredentials.m_AccessKeyID = fp_GetConfigValue("S3AccessKeyID", "").f_String();
		AWSCredentials.m_SecretKey = fp_GetConfigValue("S3SecretKey", "").f_String();

		if (AWSCredentials.m_Region.f_IsEmpty())
		{
			DMibLogWithCategory(MeteorManager, Warning, "S3Region value not specified in config, skipping S3 upload");
			return fg_Explicit();
		}

		if (AWSCredentials.m_AccessKeyID.f_IsEmpty())
		{
			DMibLogWithCategory(MeteorManager, Warning, "S3AccessKeyID value not specified in config, skipping S3 upload");
			return fg_Explicit();
		}

		if (AWSCredentials.m_SecretKey.f_IsEmpty())
		{
			DMibLogWithCategory(MeteorManager, Warning, "S3SecretKey value not specified in config, skipping S3 upload");
			return fg_Explicit();
		}

		struct CFileInfo
		{

		};

		struct CSourceCheckResults
		{
			bool m_bUpToDate = false;
			CStr m_ChecksumStr;
			CStr m_ChecksumFile;
			CDirectoryManifest m_DirectoryManifest;
		};

		g_Dispatch(*mp_FileActors) > [=]() mutable -> CSourceCheckResults
			{
				CHash_MD5 Checksum;

				auto fAddChecksum = [&](CHashDigest_MD5 const &_Checksum)
					{
						Checksum.f_AddData(_Checksum.f_GetData(), _Checksum.fs_GetSize());
					}
				;

				CBinaryStreamMemory<> Stream;
				Stream << uint32(2); // Version
				Stream << bAllowRobots;

				fAddChecksum(CHash_MD5::fs_DigestFromData(Stream.f_GetVector()));

				for (auto &File : ChecksumFiles)
					fAddChecksum(fsp_GetFileChecksum(File));

				CStr ChecksumFile = ProgramDirectory / "S3Upload.md5";
				CStr ChecksumStr = Checksum.f_GetDigest().f_GetString();

				if (CFile::fs_FileExists(ChecksumFile) && CFile::fs_ReadStringFromFile(ChecksumFile, true) == ChecksumStr)
					return {true};

				CSourceCheckResults Results;
				Results.m_ChecksumStr = ChecksumStr;
				Results.m_ChecksumFile = ChecksumFile;
				Results.m_DirectoryManifest = CDirectoryManifest::fs_GetManifest(ManifestConfig, nullptr);

				Results.m_DirectoryManifest.m_Files["robots.txt"];

				return Results;
			}
			> Continuation / [=](CSourceCheckResults const &_SourceCheckResults)
			{
				if (_SourceCheckResults.m_bUpToDate || mp_bDestroyed)
				{
					if (_SourceCheckResults.m_bUpToDate)
					{
						DLog(Info, "S3 files were already up to date");
					}
					Continuation.f_SetResult();
					return;
				}

				mp_CurlActor = fg_Construct(fg_Construct(), "S3 curl actor");
				mp_S3Actor = fg_Construct(mp_CurlActor, AWSCredentials);

				CStr BucketName = mp_Options.m_S3BucketPrefix + mp_Domain;

				mp_S3Actor(&CAwsS3Actor::f_ListBucket, BucketName) > Continuation / [=](CAwsS3Actor::CListBucket &&_Bucket)
					{
						TCSet<CStr> FilesToDelete;
						for (auto &Object : _Bucket.m_Objects)
							FilesToDelete[Object.m_Key];

						TCActorResultVector<void> UploadResults;
						for (auto &NewFile : _SourceCheckResults.m_DirectoryManifest.m_Files)
						{
							if (!NewFile.f_IsFile())
								continue;
							auto FileName = _SourceCheckResults.m_DirectoryManifest.m_Files.fs_GetKey(NewFile);

							CAwsS3Actor::CPutObjectInfo PutInfo;
							PutInfo.m_CacheControl = "no-cache"; // Rely on ETag for updated content

							auto Extension = CFile::fs_GetExtension(FileName);
							if (Extension == "gz")
							{
								FileName = CFile::fs_GetPath(FileName) / CFile::fs_GetFileNoExt(FileName);
								PutInfo.m_ContentEncoding = "gzip";
							}
							else if (_SourceCheckResults.m_DirectoryManifest.m_Files.f_FindEqual(FileName + ".gz"))
								continue;

							Extension = CFile::fs_GetExtension(FileName);

							CStr ContentType = fsp_GetContentTypeForExtension(Extension);
							if (!ContentType.f_IsEmpty())
								PutInfo.m_ContentType = ContentType;

							FilesToDelete.f_Remove(FileName);

							TCContinuation<void> UploadContinuation;
							g_Dispatch(*mp_FileActors) > [=, FullFileName = ProgramDirectory / NewFile.m_OriginalPath]() -> CByteVector
								{
									if (FileName == "robots.txt")
									{
										CStr RobotsContents = bAllowRobots ? "User-agent: *\nAllow: /" : "User-agent: *\nDisallow: /";
										return CByteVector((uint8 const *)RobotsContents.f_GetStr(), RobotsContents.f_GetLen());
									}
									return CFile::fs_ReadFile(FullFileName);
								}
								> UploadContinuation / [=](CByteVector &&_Data)
								{
									auto Extension = CFile::fs_GetExtension(FileName);
									mp_S3Actor(&CAwsS3Actor::f_PutObject, BucketName, FileName, PutInfo, fg_Move(_Data)) > UploadContinuation;
								}
							;
							UploadContinuation > UploadResults.f_AddResult();
						}

						UploadResults.f_GetResults() > Continuation / [=](TCVector<TCAsyncResult<void>> &&_UploadResults)
							{
								if (!fg_CombineResults(Continuation, fg_Move(_UploadResults)))
									return;

								TCActorResultVector<void> DeleteFilesResults;
								for (auto &File : FilesToDelete)
									mp_S3Actor(&CAwsS3Actor::f_DeleteObject, BucketName, File) > DeleteFilesResults.f_AddResult();

								DeleteFilesResults.f_GetResults() > Continuation / [=](TCVector<TCAsyncResult<void>> &&_Results)
									{
										if (!fg_CombineResults(Continuation, fg_Move(_Results)))
											return;

										g_Dispatch(*mp_FileActors) > [=]()
											{
												CFile::fs_WriteStringToFile(_SourceCheckResults.m_ChecksumFile, _SourceCheckResults.m_ChecksumStr, false);
												DLog(Info, "Done uploading static files to S3");
											}
										 	> Continuation
										;
									}
								;
							}
						;
					}
				;
			}
		;

		return Continuation;
	}
}
