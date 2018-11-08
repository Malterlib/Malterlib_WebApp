// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Meteor_App_MeteorManager_Server.h"

#include <Mib/File/ExeFS>
#include <Mib/File/VirtualFS>
#include <Mib/File/VirtualFSs/MalterlibFS>
#include <Mib/Encoding/JSONShortcuts>

namespace NMib::NMeteor::NMeteorManager
{
	namespace
	{
		constexpr uint32 gc_UpdateVersion = 22;
	}

	TCContinuation<void> CMeteorManagerActor::fp_SetupPrerequisites_UpdateAWSLambda(CAwsCredentials const &_AWSCredentials)
	{
		CStr CloudFrontDistribution = fp_GetConfigValue("AWSCloudFrontDistribution", "").f_String();
		CStr AWSLambdaRole = fp_GetConfigValue("AWSLambdaRole", "").f_String();

		if (CloudFrontDistribution.f_IsEmpty())
		{
			DMibLogWithCategory(MeteorManager, Warning, "AWSCloudFrontDistribution value not specified in config, skipping Lambda creation");
			return fg_Explicit();
		}

		if (AWSLambdaRole.f_IsEmpty())
		{
			DMibLogWithCategory(MeteorManager, Warning, "AWSLambdaRole value not specified in config, skipping Lambda creation");
			return fg_Explicit();
		}

		auto AWSCredentials = _AWSCredentials;
		AWSCredentials.m_Region = "us-east-1";

		mp_LambdaActor = fg_Construct(mp_CurlActor, AWSCredentials);

		TCContinuation<void> Continuation;

		CStr OriginRequestFunctionName = CStr("originrequest.{}{}"_f << mp_Options.m_S3BucketPrefix << mp_Domain).f_ReplaceChar('.', '_');
		TCMap<CStr, CStr> OriginRequestFiles;
		CAwsLambdaActor::CFunctionConfiguration OriginRequestConfig;

		{
			OriginRequestConfig.m_Handler = "index.handler";
			OriginRequestConfig.m_Runtime = "nodejs8.10";
			OriginRequestConfig.m_Role = AWSLambdaRole;
			OriginRequestConfig.m_MemorySizeMB = 128;
			OriginRequestConfig.m_TimeoutSeconds = 3;
			OriginRequestConfig.m_bPublish = true;

			OriginRequestFiles["index.js"] = R"----(
'use strict';
exports.handler = (event, context, callback) => {
	// Version 2

    // Extract the request from the CloudFront event that is sent to Lambda@Edge
    var request = event.Records[0].cf.request;

    // Extract the URI from the request
    var olduri = request.uri;

    if (/\/(.*\/)*.*\..*/.test(olduri))
        return callback(null, request); // Something with . in name

    // Match any '/' that occurs at the end of a URI. Replace it with a default index
    var newuri;
    if (/\/$/.test(olduri))
        newuri = olduri.replace(/\/$/, '\/index.html');
    else
        newuri = olduri.replace(/$/, '\/index.html');

    // Log the URI as received by CloudFront and the new URI to be used to fetch from origin
    //console.log("Old URI: " + olduri);
    //console.log("New URI: " + newuri);

    // Replace the received URI with the URI that includes the index page
    request.uri = newuri;

    // Return to CloudFront
    return callback(null, request);
};
)----";
		}

		CStr OriginResponseFunctionName = CStr("originresponse.{}{}"_f << mp_Options.m_S3BucketPrefix << mp_Domain).f_ReplaceChar('.', '_');
		TCMap<CStr, CStr> OriginResponseFiles;
		CAwsLambdaActor::CFunctionConfiguration OriginResponseConfig;

		{
			OriginResponseConfig.m_Handler = "index.handler";
			OriginResponseConfig.m_Runtime = "nodejs8.10";
			OriginResponseConfig.m_Role = AWSLambdaRole;
			OriginResponseConfig.m_MemorySizeMB = 128;
			OriginResponseConfig.m_TimeoutSeconds = 3;
			OriginResponseConfig.m_bPublish = true;

			CStr ContentSecurityPolicy = "default-src 'none' ;";
			ContentSecurityPolicy += " img-src 'self' data: *.{0} {0} {1} ;"_f << mp_Domain << mp_Options.m_ContentSecurity_ImgSrc;
			ContentSecurityPolicy += " font-src 'self' data: *.{0} {0} {1} ;"_f << mp_Domain << mp_Options.m_ContentSecurity_FontSrc;
			ContentSecurityPolicy += " media-src 'self' *.{0} {0} {1} ;"_f << mp_Domain << mp_Options.m_ContentSecurity_MediaSrc;
			ContentSecurityPolicy += " script-src 'self' *.{0} {0} {1};"_f << mp_Domain << mp_Options.m_ContentSecurity_ScriptSrc;
			ContentSecurityPolicy += " style-src 'self' *.{0} {0} {1} ;"_f << mp_Domain << mp_Options.m_ContentSecurity_StyleSrc;
			ContentSecurityPolicy += " frame-src 'self' *.{0} {0} {1} ;"_f << mp_Domain << mp_Options.m_ContentSecurity_FrameSrc;
			ContentSecurityPolicy += " connect-src 'self' *.{0} {0} {1} ;"_f << mp_Domain << mp_Options.m_ContentSecurity_ConnectSrc;
			ContentSecurityPolicy += " object-src 'none' {} ;"_f << mp_Options.m_ContentSecurity_ObjectSrc;

			CStr AccessControl;
			if (!mp_Options.m_AccessControl_AllowMethods.f_IsEmpty())
				AccessControl += "	headers['access-control-allow-methods'] = [{{key: 'Access-Control-Allow-Methods', value: '{}'}];\n"_f << mp_Options.m_AccessControl_AllowMethods;
			if (!mp_Options.m_AccessControl_AllowHeaders.f_IsEmpty())
				AccessControl += "	headers['access-control-allow-headers'] = [{{key: 'Access-Control-Allow-Headers', value: '{}'}];\n"_f << mp_Options.m_AccessControl_AllowHeaders;
			if (!mp_Options.m_AccessControl_AllowOrigin.f_IsEmpty())
				AccessControl += "	headers['access-control-allow-origin'] = [{{key: 'Access-Control-Allow-Origin', value: '{}'}];\n"_f << mp_Options.m_AccessControl_AllowOrigin;
			if (!mp_Options.m_AccessControl_MaxAge.f_IsEmpty())
				AccessControl += "	headers['access-control-max-age'] = [{{key: 'Access-Control-Max-Age', value: '{}'}];\n"_f << mp_Options.m_AccessControl_MaxAge;

			OriginResponseFiles["index.js"] = CStr(R"----(
'use strict';
exports.handler = (event, context, callback) => {

	// Get contents of response
	const response = event.Records[0].cf.response;
	const headers = response.headers;

	// Set new headers
	headers['strict-transport-security'] = [{key: 'Strict-Transport-Security', value: 'max-age=63072000; includeSubdomains; preload'}];
	headers['content-security-policy'] = [{key: 'Content-Security-Policy', value: "{ContentSecurityPolicy}"}];
	headers['x-content-type-options'] = [{key: 'X-Content-Type-Options', value: 'nosniff'}];
	headers['x-frame-options'] = [{key: 'X-Frame-Options', value: 'DENY'}];
	headers['x-xss-protection'] = [{key: 'X-XSS-Protection', value: '1; mode=block'}];
	headers['referrer-policy'] = [{key: 'Referrer-Policy', value: 'same-origin'}];

{AccessControl}

	//Return modified response
	callback(null, response);
};
)----").f_Replace("{ContentSecurityPolicy}", ContentSecurityPolicy).f_Replace("{AccessControl}", AccessControl);
		}

		mp_LambdaActor(&CAwsLambdaActor::f_CreateOrUpdateFunction, OriginRequestFunctionName, OriginRequestFiles, OriginRequestConfig)
			+ mp_LambdaActor(&CAwsLambdaActor::f_CreateOrUpdateFunction, OriginResponseFunctionName, OriginResponseFiles, OriginResponseConfig)
			> Continuation % "Update AWS Lambda functions" / [=](CAwsLambdaActor::CFunctionInfo &&_OriginRequestInfo, CAwsLambdaActor::CFunctionInfo &&_OriginResponseInfo)
			{
				if
					(
					 	_OriginRequestInfo.m_Version.f_IsEmpty()
					 	|| _OriginRequestInfo.m_Version == "$LATEST"
					 	|| _OriginResponseInfo.m_Version.f_IsEmpty()
					 	|| _OriginResponseInfo.m_Version == "$LATEST"
					)
				{
					Continuation.f_SetResult();
					return;
				}

				NContainer::TCMap<CAwsCloudFrontActor::EFunctionEventType, NStr::CStr> FunctionAssociations;
				FunctionAssociations[CAwsCloudFrontActor::EFunctionEventType_OriginRequest] = _OriginRequestInfo.m_Arn;
				FunctionAssociations[CAwsCloudFrontActor::EFunctionEventType_OriginResponse] = _OriginResponseInfo.m_Arn;

				mp_CloudFrontActor(&CAwsCloudFrontActor::f_UpdateDistributionLambdaFunctions, CloudFrontDistribution, FunctionAssociations)
					> Continuation % "Associate AWS Lambda function with CloudFront distribution" / [=]
					{
						Continuation.f_SetResult();
					}
				;
			}
		;

		return Continuation;
	}

	TCContinuation<void> CMeteorManagerActor::fp_SetupPrerequisites_UploadS3FileChangeNotifications()
	{
		CStr ProgramDirectory = CFile::fs_GetProgramDirectory();

		TCActorResultVector<void> Results;

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

			if (Package.m_ExternalRoot.f_IsEmpty())
				continue;

			CStr Root = CFile::fs_GetExpandedPath(ProgramDirectory / Package.m_ExternalRoot);

			TCContinuation<void> RegisterContinuation;
			mp_FileChangeNotificationActor
				(
				 	&CFileChangeNotificationActor::f_RegisterForChanges
				 	, Root
				 	, EFileChange_All
					, g_ActorFunctor > [=](TCVector<CFileChangeNotification::CNotification> const &_Changes) -> TCContinuation<void>
				 	{
						if (!mp_bInitialS3UploadDone)
							return fg_Explicit();
						TCContinuation<void> Continuation;
						mp_S3UploadSequencer > [=]() -> TCContinuation<void>
							{
								DMibLogWithCategory(MeteorManager, Info, "Updating S3 upload due to file changes");
								return fp_SetupPrerequisites_UploadS3Perform();
							}
							> [=](TCAsyncResult<void> &&_Result)
							{
								if (!_Result)
									DMibLogWithCategory(MeteorManager, Error, "Failed to update S3 upload due to file changes: {}", _Result.f_GetExceptionStr());
								else
									DMibLogWithCategory(MeteorManager, Info, "S3 upload due to file changes finished");

								Continuation.f_SetResult();
							}
						;

						return Continuation;
					}
				 	, CFileChangeNotificationActor::CCoalesceSettings{}
				)
				> RegisterContinuation / [=](NConcurrency::CActorSubscription &&_Subscription)
				{
					mp_S3FileChangeNotificationSubscriptions.f_Insert(fg_Move(_Subscription));
					RegisterContinuation.f_SetResult();
				}
			;

			RegisterContinuation > Results.f_AddResult();
		}

		TCContinuation<void> Continuation;
		Results.f_GetResults() > Continuation / [=](TCVector<TCAsyncResult<void>> &&_Results)
			{
				if (!fg_CombineResults(Continuation, fg_Move(_Results)))
					return;

				Continuation.f_SetResult();
			}
		;

		return Continuation;
	}

	TCContinuation<void> CMeteorManagerActor::fp_SetupPrerequisites_UploadS3()
	{
		return mp_S3UploadSequencer > [=]() -> TCContinuation<void>
			{
				mp_bInitialS3UploadDone = true;
				return fp_SetupPrerequisites_UploadS3Perform();
			}
		;
	}

	TCContinuation<void> CMeteorManagerActor::fp_SetupPrerequisites_UploadS3Perform()
	{
		TCContinuation<void> Continuation;

		CStr ProgramDirectory = CFile::fs_GetProgramDirectory();

		TCSet<CStr> ChecksumFiles;

		TCSet<CStr> Roots;

		bool bRawTarGz = false;

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

			if (Package.m_ExcludeGzipPatterns.f_Contains("*.tar.gz") >= 0)
				bRawTarGz = true;

			CStr Root;
			if (Package.m_ExternalRoot.f_IsEmpty())
				Root = ProgramDirectory / Package.f_GetName();
			else
				Root = CFile::fs_GetExpandedPath(ProgramDirectory / Package.m_ExternalRoot);

			Roots[Root];
		}

		CStr RootPath = ProgramDirectory;

		for (auto &Root : Roots)
			RootPath = CFile::fs_GetCommonPath(RootPath, Root);

		CDirectoryManifestConfig ManifestConfig;
		ManifestConfig.m_IncludeWildcards.f_Clear();
		ManifestConfig.m_Root = RootPath;
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

			CStr Root;
			if (Package.m_ExternalRoot.f_IsEmpty())
				Root = ProgramDirectory / Package.f_GetName();
			else
				Root = CFile::fs_GetExpandedPath(ProgramDirectory / Package.m_ExternalRoot);

			CStr RelativePath = CFile::fs_MakePathRelative(Root, RootPath);

			ManifestConfig.m_IncludeWildcards[RelativePath / "^*"_f] = StaticPath;

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

		CAwsCredentials AWSCredentials;

		AWSCredentials.m_Region = fp_GetConfigValue("AWSS3Region", "").f_String();
		AWSCredentials.m_AccessKeyID = fp_GetConfigValue("AWSAccessKeyID", "").f_String();
		AWSCredentials.m_SecretKey = fp_GetConfigValue("AWSSecretKey", "").f_String();
		CStr CloudFrontDistribution = fp_GetConfigValue("AWSCloudFrontDistribution", "").f_String();

		if (AWSCredentials.m_Region.f_IsEmpty())
		{
			DMibLogWithCategory(MeteorManager, Warning, "AWSS3Region value not specified in config, skipping S3 upload");
			return fg_Explicit();
		}

		if (AWSCredentials.m_AccessKeyID.f_IsEmpty())
		{
			DMibLogWithCategory(MeteorManager, Warning, "AWSAccessKeyID value not specified in config, skipping S3 upload");
			return fg_Explicit();
		}

		if (AWSCredentials.m_SecretKey.f_IsEmpty())
		{
			DMibLogWithCategory(MeteorManager, Warning, "AWSSecretKey value not specified in config, skipping S3 upload");
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

		g_Dispatch(*mp_FileActors) > [=, Options = mp_Options, Domain = mp_Domain]() mutable -> CSourceCheckResults
			{
				CHash_MD5 Checksum;

				auto fAddChecksum = [&](CHashDigest_MD5 const &_Checksum)
					{
						Checksum.f_AddData(_Checksum.f_GetData(), _Checksum.fs_GetSize());
					}
				;

				CDirectoryManifest DirectoryManifest = CDirectoryManifest::fs_GetManifest(ManifestConfig, nullptr);
				DirectoryManifest.m_Files["robots.txt"];

				CBinaryStreamMemory<> Stream;
				Stream << gc_UpdateVersion; // Version
				Stream << bAllowRobots;
				Stream << CloudFrontDistribution;
				Stream << Options.m_ContentSecurity_ImgSrc;
				Stream << Options.m_ContentSecurity_MediaSrc;
				Stream << Options.m_ContentSecurity_FontSrc;
				Stream << Domain;
				Stream << Options.m_ContentSecurity_ScriptSrc;
				Stream << Options.m_ContentSecurity_StyleSrc;
				Stream << Options.m_ContentSecurity_FrameSrc;
				Stream << Options.m_ContentSecurity_ConnectSrc;
				Stream << Options.m_ContentSecurity_ObjectSrc;
				Stream << bRawTarGz;
				Stream << DirectoryManifest;

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
				Results.m_DirectoryManifest = fg_Move(DirectoryManifest);

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
				mp_CloudFrontActor = fg_Construct(mp_CurlActor, AWSCredentials);

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
							if (!bRawTarGz)
							{
								if (Extension == "gz")
								{
									FileName = CFile::fs_GetPath(FileName) / CFile::fs_GetFileNoExt(FileName);
									PutInfo.m_ContentEncoding = "gzip";
								}
								else if (_SourceCheckResults.m_DirectoryManifest.m_Files.f_FindEqual(FileName + ".gz"))
									continue;
								Extension = CFile::fs_GetExtension(FileName);
							}

							CStr ContentType = fsp_GetContentTypeForExtension(Extension);
							if (!ContentType.f_IsEmpty())
								PutInfo.m_ContentType = ContentType;

							FilesToDelete.f_Remove(FileName);

							TCContinuation<void> UploadContinuation;
							g_Dispatch(*mp_FileActors) > [=, FullFileName = RootPath / NewFile.m_OriginalPath]() -> CByteVector
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

								DeleteFilesResults.f_GetResults()
									+ fp_SetupPrerequisites_UpdateAWSLambda(AWSCredentials)
									> Continuation / [=](TCVector<TCAsyncResult<void>> &&_Results, CVoidTag)
									{
										if (!fg_CombineResults(Continuation, fg_Move(_Results)))
											return;

										TCContinuation<void> CloudFrontInvalidateResult;
										if (CloudFrontDistribution.f_IsEmpty())
											CloudFrontInvalidateResult.f_SetResult();
										else
										{
											TCVector<CStr> PathsToInvalidate = {"/*"};
											mp_CloudFrontActor(&CAwsCloudFrontActor::f_CreateInvalidation, CloudFrontDistribution, PathsToInvalidate) > CloudFrontInvalidateResult / [=]
												{
													CloudFrontInvalidateResult.f_SetResult();
												}
											;
										}

										CloudFrontInvalidateResult > Continuation / [=]
											{
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
			}
		;

		return Continuation;
	}
}
