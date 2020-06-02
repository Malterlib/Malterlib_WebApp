// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_WebApp_App_WebAppManager_Server.h"

#include <Mib/File/ExeFS>
#include <Mib/File/VirtualFS>
#include <Mib/File/VirtualFSs/MalterlibFS>
#include <Mib/Encoding/JSONShortcuts>

namespace NMib::NWebApp::NWebAppManager
{
	namespace
	{
		uint32 gc_UpdateVersion = 24;
	}

	TCFuture<void> CWebAppManagerActor::fp_SetupPrerequisites_UpdateAWSLambda(CAwsCredentials const &_AWSCredentials)
	{
		auto OnResume = g_OnResume / [&]
			{
				if (f_IsDestroyed())
					DMibError("Shutting down");
			}
		;

		CStr CloudFrontDistribution = fp_GetConfigValue("AWSCloudFrontDistribution", "").f_String();
		CStr AWSLambdaRole = fp_GetConfigValue("AWSLambdaRole", "").f_String();

		if (CloudFrontDistribution.f_IsEmpty())
		{
			DMibLogWithCategory(WebAppManager, Warning, "AWSCloudFrontDistribution value not specified in config, skipping Lambda creation");
			co_return {};
		}

		if (AWSLambdaRole.f_IsEmpty())
		{
			DMibLogWithCategory(WebAppManager, Warning, "AWSLambdaRole value not specified in config, skipping Lambda creation");
			co_return {};
		}

		auto AWSCredentials = _AWSCredentials;
		AWSCredentials.m_Region = "us-east-1";

		mp_LambdaActor = fg_Construct(*mp_CurlActors, AWSCredentials);

		CStr OriginRequestFunctionName = CStr("originrequest.{}{}"_f << mp_Options.m_S3BucketPrefix << mp_Domain).f_ReplaceChar('.', '_');
		TCMap<CStr, CStr> OriginRequestFiles;
		CAwsLambdaActor::CFunctionConfiguration OriginRequestConfig;

		{
			OriginRequestConfig.m_Handler = "index.handler";
			OriginRequestConfig.m_Runtime = "nodejs12.x";
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
			OriginResponseConfig.m_Runtime = "nodejs12.x";
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
			ContentSecurityPolicy += " child-src 'self' *.{0} {0} {1} ;"_f << mp_Domain << mp_Options.m_ContentSecurity_ChildSrc;
			ContentSecurityPolicy += " form-action 'self' *.{0} {0} {1} ;"_f << mp_Domain << mp_Options.m_ContentSecurity_FormAction;
			ContentSecurityPolicy += " object-src 'none' {} ;"_f << mp_Options.m_ContentSecurity_ObjectSrc;

			if (mp_Options.m_ContentSecurity_ReportURI)
			{
				CStr ContentSecurityReportURI = mp_Options.m_ContentSecurity_ReportURI;
				ContentSecurityReportURI = ContentSecurityReportURI.f_Replace("{DomainName}", mp_Domain);
				ContentSecurityReportURI = ContentSecurityReportURI.f_Replace("{SSLPortRewrite}", "");
				ContentSecurityPolicy += " report-uri {} ;"_f << ContentSecurityReportURI;
			}

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

		auto [OriginRequestInfo, OriginResponseInfo] = co_await
			(
				(
					mp_LambdaActor(&CAwsLambdaActor::f_CreateOrUpdateFunction, OriginRequestFunctionName, OriginRequestFiles, OriginRequestConfig)
					+ mp_LambdaActor(&CAwsLambdaActor::f_CreateOrUpdateFunction, OriginResponseFunctionName, OriginResponseFiles, OriginResponseConfig)
				)
				% "Update AWS Lambda functions"
			)
		;

		if
			(
				OriginRequestInfo.m_Version.f_IsEmpty()
				|| OriginRequestInfo.m_Version == "$LATEST"
				|| OriginResponseInfo.m_Version.f_IsEmpty()
				|| OriginResponseInfo.m_Version == "$LATEST"
			)
		{
			co_return {};
		}

		NContainer::TCMap<CAwsCloudFrontActor::EFunctionEventType, NStr::CStr> FunctionAssociations;
		FunctionAssociations[CAwsCloudFrontActor::EFunctionEventType_OriginRequest] = OriginRequestInfo.m_Arn;
		FunctionAssociations[CAwsCloudFrontActor::EFunctionEventType_OriginResponse] = OriginResponseInfo.m_Arn;

		co_await
			(
				mp_CloudFrontActor(&CAwsCloudFrontActor::f_UpdateDistributionLambdaFunctions, CloudFrontDistribution, FunctionAssociations)
				% "Associate AWS Lambda function with CloudFront distribution"
			)
		;

		co_return {};
	}

	TCFuture<void> CWebAppManagerActor::fp_SetupPrerequisites_UploadS3FileChangeNotifications()
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

			(
				self / [=]() -> TCFuture<void>
				{
					auto Subscription = co_await mp_FileChangeNotificationActor
						(
							&CFileChangeNotificationActor::f_RegisterForChanges
							, Root
							, EFileChange_All
							, g_ActorFunctor / [=](TCVector<CFileChangeNotification::CNotification> const &_Changes) -> TCFuture<void>
							{
								if (!mp_bInitialS3UploadDone || mp_bPendingS3Upload)
									co_return {};
								mp_bPendingS3Upload = true;

								auto Result = co_await
									(
										mp_S3UploadSequencer / [=]() -> TCFuture<void>
										{
											mp_bPendingS3Upload = false;
											DMibLogWithCategory(WebAppManager, Info, "Updating S3 upload due to file changes");
											co_await self(&CWebAppManagerActor::fp_SetupPrerequisites_UploadS3Perform);
											co_return {};
										}
									)
									.f_Wrap()
								;

								if (!Result)
									DMibLogWithCategory(WebAppManager, Error, "Failed to update S3 upload due to file changes: {}", Result.f_GetExceptionStr());
								else
									DMibLogWithCategory(WebAppManager, Info, "S3 upload due to file changes finished");

								co_return {};
							}
							, CFileChangeNotificationActor::CCoalesceSettings{}
						)
					;
					mp_S3FileChangeNotificationSubscriptions.f_Insert(fg_Move(Subscription));

					co_return {};
				}
			)
			> Results.f_AddResult();
		}

		co_await Results.f_GetResults() | g_Unwrap;

		co_return {};
	}

	TCFuture<void> CWebAppManagerActor::fp_SetupPrerequisites_UploadS3()
	{
		return mp_S3UploadSequencer / [=]() -> TCFuture<void>
			{
				mp_bInitialS3UploadDone = true;
				co_await self(&CWebAppManagerActor::fp_SetupPrerequisites_UploadS3Perform);
				co_return {};
			}
		;
	}

	namespace
	{
		struct CChecksums
		{
			CHashDigest_SHA256 m_SHA256;
			CHashDigest_MD5 m_MD5;

			template <typename tf_CStream>
			void f_Stream(tf_CStream &_Stream)
			{
				_Stream % m_SHA256;
				_Stream % m_MD5;
			}
		};

		struct CSourceCheckResults
		{
			bool m_bUpToDate = false;
			CStr m_ChecksumStr;
			CStr m_ChecksumFile;
			CDirectoryManifest m_DirectoryManifest;
			TCMap<CStr, CChecksums> m_FileChecksums;
		};
	}

	TCFuture<void> CWebAppManagerActor::fp_SetupPrerequisites_UploadS3Perform()
	{
		auto OnResume = g_OnResume / [&]
			{
				if (f_IsDestroyed())
					DMibError("Shutting down");
			}
		;

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

		TCMap<CStr, int64> UploadPriorities;

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

			for (auto &UploadPriority : Package.m_UploadS3Priority)
				UploadPriorities[StaticPath / Package.m_UploadS3Priority.fs_GetKey(UploadPriority)] = UploadPriority;

			ManifestConfig.m_IncludeWildcards[RelativePath / "^*"_f] = StaticPath;

			CStr ChecksumFile = ProgramDirectory / ("{}.tar.gz"_f << Package.f_GetName());
			ChecksumFiles[ChecksumFile];

			if (bIsMainServer && Package.m_bAllowRobots && mp_bAllowRobots)
				bAllowRobots = true;
		}

		if (ManifestConfig.m_IncludeWildcards.f_IsEmpty())
			co_return {};

		DMibLogWithCategory(S3Upload, Info, "Uploading static files to S3");
		NTime::CClock GlobalClock{true};

		CAwsCredentials AWSCredentials;

		AWSCredentials.m_Region = fp_GetConfigValue("AWSS3Region", "").f_String();
		AWSCredentials.m_AccessKeyID = fp_GetConfigValue("AWSAccessKeyID", "").f_String();
		AWSCredentials.m_SecretKey = fp_GetConfigValue("AWSSecretKey", "").f_String();
		CStr CloudFrontDistribution = fp_GetConfigValue("AWSCloudFrontDistribution", "").f_String();

		if (AWSCredentials.m_Region.f_IsEmpty())
		{
			DMibLogWithCategory(WebAppManager, Warning, "AWSS3Region value not specified in config, skipping S3 upload");
			co_return {};
		}

		if (AWSCredentials.m_AccessKeyID.f_IsEmpty())
		{
			DMibLogWithCategory(WebAppManager, Warning, "AWSAccessKeyID value not specified in config, skipping S3 upload");
			co_return {};
		}

		if (AWSCredentials.m_SecretKey.f_IsEmpty())
		{
			DMibLogWithCategory(WebAppManager, Warning, "AWSSecretKey value not specified in config, skipping S3 upload");
			co_return {};
		}

		DMibLogWithCategory(S3Upload, Info, "Getting source checksums");
		NTime::CClock Clock{true};

		CSourceCheckResults SourceCheckResults = co_await
			(
				g_Dispatch(*mp_FileActors) / [=, Options = mp_Options, Domain = mp_Domain]() mutable -> CSourceCheckResults
				{
					CHash_MD5 Checksum;

					auto fAddChecksum = [&](CHashDigest_MD5 const &_Checksum)
						{
							Checksum.f_AddData(_Checksum.f_GetData(), _Checksum.fs_GetSize());
						}
					;

					CDirectoryManifest PreviousDirectoryManifest;
					CDirectoryManifest DirectoryManifest;

					{
						CStr PreviousManifestFile = ProgramDirectory / "S3UploadPreviousManifest.bin";

						bool bPreviousExists = CFile::fs_FileExists(PreviousManifestFile);

						if (bPreviousExists)
							PreviousDirectoryManifest = TCBinaryStreamFile<>::fs_ReadFile<CDirectoryManifest>(PreviousManifestFile);

						DirectoryManifest = CDirectoryManifest::fs_GetManifest(ManifestConfig, nullptr, nullptr, NFile::EFileOpen_None, &PreviousDirectoryManifest);
						if (!DirectoryManifest.m_Files.f_FindEqual("robots.txt"))
						{
							auto &ManifestFile = DirectoryManifest.m_Files["robots.txt"];
							ManifestFile.m_SymlinkData = bAllowRobots ? "User-agent: *\nAllow: /" : "User-agent: *\nDisallow: /";
							ManifestFile.m_Digest = CHash_SHA256::fs_DigestFromData(ManifestFile.m_SymlinkData.f_GetStr(), ManifestFile.m_SymlinkData.f_GetLen());
						}

						TCBinaryStreamFile<>::fs_WriteFile(DirectoryManifest, PreviousManifestFile + ".tmp");

						if (bPreviousExists)
							CFile::fs_AtomicReplaceFile(PreviousManifestFile + ".tmp", PreviousManifestFile);
						else
							CFile::fs_RenameFile(PreviousManifestFile + ".tmp", PreviousManifestFile);
					}

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
					Stream << Options.m_ContentSecurity_ChildSrc;
					Stream << Options.m_ContentSecurity_FormAction;
					Stream << Options.m_ContentSecurity_ReportURI;
					Stream << Options.m_AccessControl_AllowMethods;
					Stream << Options.m_AccessControl_AllowHeaders;
					Stream << Options.m_AccessControl_AllowOrigin;
					Stream << Options.m_AccessControl_MaxAge;
					Stream << bRawTarGz;
					Stream << DirectoryManifest;

					fAddChecksum(CHash_MD5::fs_DigestFromData(Stream.f_GetVector()));

					for (auto &File : ChecksumFiles)
						fAddChecksum(fsp_GetFileChecksum(File));

					CStr ChecksumFile = ProgramDirectory / "S3Upload.md5";
					CStr ChecksumStr = Checksum.f_GetDigest().f_GetString();

					if (CFile::fs_FileExists(ChecksumFile) && CFile::fs_ReadStringFromFile(ChecksumFile, true) == ChecksumStr)
						return {true};

					TCMap<CStr, CChecksums> FileChecksums;
					{
						TCMap<CStr, CChecksums> PreviousChecksums;
						CStr PreviousChecksumFile = ProgramDirectory / "S3UploadChecksums.bin";

						bool bPreviousExists = CFile::fs_FileExists(PreviousChecksumFile);
						if (bPreviousExists)
							PreviousChecksums = TCBinaryStreamFile<>::fs_ReadFile<TCMap<CStr, CChecksums>>(PreviousChecksumFile);

						for (auto &File : DirectoryManifest.m_Files)
						{
							if (!File.f_IsFile())
								continue;

							auto const &FileName = File.f_GetFileName();

							auto const *pPreviousChecksum = PreviousChecksums.f_FindEqual(FileName);
							if (pPreviousChecksum && File.m_Digest == pPreviousChecksum->m_SHA256)
							{
								FileChecksums[FileName] = {File.m_Digest, pPreviousChecksum->m_MD5};
								continue;
							}

							if (FileName == "robots.txt" && !File.m_SymlinkData.f_IsEmpty())
								FileChecksums[FileName] = {File.m_Digest, CHash_MD5::fs_DigestFromData(File.m_SymlinkData.f_GetStr(), File.m_SymlinkData.f_GetLen())};
							else
								FileChecksums[FileName] = {File.m_Digest, CFile::fs_GetFileChecksum(RootPath / File.m_OriginalPath)};
						}

						TCBinaryStreamFile<>::fs_WriteFile(FileChecksums, PreviousChecksumFile + ".tmp");

						if (bPreviousExists)
							CFile::fs_AtomicReplaceFile(PreviousChecksumFile + ".tmp", PreviousChecksumFile);
						else
							CFile::fs_RenameFile(PreviousChecksumFile + ".tmp", PreviousChecksumFile);
					}

					CSourceCheckResults Results;
					Results.m_ChecksumStr = ChecksumStr;
					Results.m_ChecksumFile = ChecksumFile;
					Results.m_DirectoryManifest = fg_Move(DirectoryManifest);
					Results.m_FileChecksums = fg_Move(FileChecksums);

					return Results;
				}
			)
		;

		DMibLogWithCategory(S3Upload, Info, "Getting source checksums {fe2} s", Clock.f_GetTime());
		Clock.f_Start();

		if (SourceCheckResults.m_bUpToDate)
		{
			DMibLogWithCategory(S3Upload, Info, "S3 files were already up to date");
			co_return {};
		}

		if (!mp_CurlActors.f_IsConstructed())
			mp_CurlActors.f_Construct(fg_Construct(fg_Construct(), "S3 curl actor"));

		if (!mp_S3Actors.f_IsConstructed())
		{
			mp_S3Actors.f_ConstructFunctor
				(
					[&]
					{
						return fg_Construct(*mp_CurlActors, AWSCredentials);
					}
				)
			;
		}

		mp_CloudFrontActor = fg_Construct(*mp_CurlActors, AWSCredentials);

		CStr BucketName = mp_Options.m_S3BucketPrefix + mp_Domain;

		DMibLogWithCategory(S3Upload, Info, "Listing bucket");
		auto Bucket = co_await (*mp_S3Actors)(&CAwsS3Actor::f_ListBucket, BucketName);

		DMibLogWithCategory(S3Upload, Info, "Listing bucket {fe2} s", Clock.f_GetTime());
		Clock.f_Start();

		TCActorResultMap<CStr, CAwsS3Actor::CObjectInfoMetaData> MetaDataResults;

		TCMap<CStr, CStr> ExistingObjects;
		for (auto &Object : Bucket.m_Objects)
			ExistingObjects[Object.m_Key] = Object.m_ETag;

		TCSet<CStr> FilesToUpdate;

		mint nMetaDataQueries = 0;
		for (auto &NewFile : SourceCheckResults.m_DirectoryManifest.m_Files)
		{
			if (!NewFile.f_IsFile())
				continue;

			auto &FileName = SourceCheckResults.m_DirectoryManifest.m_Files.fs_GetKey(NewFile);
			auto DestinationFileName = FileName;

			if (!bRawTarGz)
			{
				if (CFile::fs_GetExtension(FileName) == "gz")
					DestinationFileName = CFile::fs_GetPath(FileName) / CFile::fs_GetFileNoExt(FileName);
				else if (SourceCheckResults.m_DirectoryManifest.m_Files.f_FindEqual(FileName + ".gz"))
					continue;
			}

			auto pExistingObject = ExistingObjects.f_FindEqual(DestinationFileName);
			if (!pExistingObject)
			{
				FilesToUpdate[DestinationFileName];
				continue;
			}

			auto *pNewChecksum = SourceCheckResults.m_FileChecksums.f_FindEqual(FileName);
			DMibCheck(pNewChecksum);

			if (!pNewChecksum || pNewChecksum->m_MD5.f_GetString() != *pExistingObject)
			{
				FilesToUpdate[DestinationFileName];
				continue;
			}

			++nMetaDataQueries;
			(*mp_S3Actors)(&CAwsS3Actor::f_GetObjectMetaData, BucketName, DestinationFileName) > MetaDataResults.f_AddResult(DestinationFileName);
		}

		if (nMetaDataQueries)
			DMibLogWithCategory(S3Upload, Info, "Querying object meta data for {} objects", nMetaDataQueries);

		auto MetaData = co_await (MetaDataResults.f_GetResults() % "Failed to get file meta data");

		if (nMetaDataQueries)
			DMibLogWithCategory(S3Upload, Info, "Querying object meta data for {} objects {fe2} s", nMetaDataQueries, Clock.f_GetTime());
		Clock.f_Start();

		TCSet<CStr> FilesToDelete;
		for (auto &Object : Bucket.m_Objects)
			FilesToDelete[Object.m_Key];

		TCMap<int64, TCVector<TCFunctionMutable<TCFuture<void> ()>>> ToUpload;

		for (auto &NewFile : SourceCheckResults.m_DirectoryManifest.m_Files)
		{
			if (!NewFile.f_IsFile())
				continue;

			auto FileName = SourceCheckResults.m_DirectoryManifest.m_Files.fs_GetKey(NewFile);

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
				else if (SourceCheckResults.m_DirectoryManifest.m_Files.f_FindEqual(FileName + ".gz"))
					continue;
				Extension = CFile::fs_GetExtension(FileName);
			}

			FilesToDelete.f_Remove(FileName);

			CStr ContentType = fsp_GetContentTypeForExtension(Extension);
			if (!ContentType.f_IsEmpty())
				PutInfo.m_ContentType = ContentType;
			else
				PutInfo.m_ContentType = "application/octet-stream"; // Default to safe octet-stream

			if (!FilesToUpdate.f_FindEqual(FileName))
			{
				auto pMetaData = MetaData.f_FindEqual(FileName);
				if (pMetaData && *pMetaData)
				{
					auto &MetaData = **pMetaData;
					if
						(
							MetaData.m_CacheControl.f_Get("") == PutInfo.m_CacheControl.f_Get("")
							&& MetaData.m_ContentEncoding.f_Get("") == PutInfo.m_ContentEncoding.f_Get("")
							&& MetaData.m_ContentType.f_Get("") == PutInfo.m_ContentType.f_Get("")
						)
					{
						continue; // Already up to date
					}
				}
			}

			int64 Priority = TCLimitsInt<int64>::mc_Max;

			for (auto &UploadPriority : UploadPriorities)
			{
				auto &Pattern = UploadPriorities.fs_GetKey(UploadPriority);
				if (NStr::fg_StrMatchWildcard(FileName.f_GetStr(), Pattern.f_GetStr()) == EMatchWildcardResult_WholeStringMatchedAndPatternExhausted)
					Priority = fg_Min(Priority, UploadPriority);
			}

			if (Priority == TCLimitsInt<int64>::mc_Max)
				Priority = 0;

			// Limit the number of files held in memory to limit memory usage
			ToUpload[Priority].f_Insert
				(
					[=, ExpectedChecksum = NewFile.m_Digest]() -> TCFuture<void>
					{
						auto OnResume = g_OnResume / [&]
							{
								if (f_IsDestroyed())
									DMibError("Shutting down");
							}
						;

						DMibLogWithCategory(S3Upload, Info, "Uploading file with priority {}: '{}'", Priority, FileName);

						CByteVector ReadData;

						if (FileName == "robots.txt" && !NewFile.m_SymlinkData.f_IsEmpty())
							ReadData = CByteVector((uint8 *)NewFile.m_SymlinkData.f_GetStr(), NewFile.m_SymlinkData.f_GetLen());
						else
						{
							ReadData = co_await
								(
									g_Dispatch(*mp_FileActors) / [=, FullFileName = RootPath / NewFile.m_OriginalPath]() -> CByteVector
									{
										auto FileData = CFile::fs_ReadFile(FullFileName);

										if (CHash_SHA256::fs_DigestFromData(FileData) != ExpectedChecksum)
											DMibError("Aborting file upload due to changed file contents");

										return FileData;
									}
									% ("Failed to read '{}'"_f << FileName)
								)
							;
						}

						co_await ((*mp_S3Actors)(&CAwsS3Actor::f_PutObject, BucketName, FileName, PutInfo, fg_Move(ReadData)) % ("Failed to upload '{}'"_f << FileName));

						co_return {};
					}
				)
			;
		}

		TCActorResultVector<void> UploadResults;

		for (auto &PriorityUploadList : ToUpload)
		{
			mp_S3PrioritySequencer / [=]() mutable -> TCFuture<void>
				{
					auto OnResume = g_OnResume / [&]
						{
							if (f_IsDestroyed())
								DMibError("Shutting down");
						}
					;

					TCActorResultVector<void> UploadResults;

					for (auto &fUpload : PriorityUploadList)
						mp_S3FileReadSequencer / fg_Move(fUpload) > UploadResults.f_AddResult();

					co_await UploadResults.f_GetResults() | g_Unwrap;

					co_return {};
				}
				> UploadResults.f_AddResult()
			;
		}

		bool bUploadFiles = !UploadResults.f_IsEmpty();
		if (bUploadFiles)
			DMibLogWithCategory(S3Upload, Info, "Reading files and uploading");

		co_await UploadResults.f_GetResults() | g_Unwrap;

		if (bUploadFiles)
			DMibLogWithCategory(S3Upload, Info, "Reading files and uploading {fe2} s", Clock.f_GetTime());
		Clock.f_Start();

		TCActorResultVector<void> DeleteFilesResults;
		for (auto &File : FilesToDelete)
			(*mp_S3Actors)(&CAwsS3Actor::f_DeleteObject, BucketName, File) > DeleteFilesResults.f_AddResult();

		bool bDeleteFiles = !DeleteFilesResults.f_IsEmpty();
		if (bDeleteFiles)
			DMibLogWithCategory(S3Upload, Info, "Deleting files and updating Lambda@Edge");
		else
			DMibLogWithCategory(S3Upload, Info, "Updating Lambda@Edge");

		auto [Results, Dummy] = co_await
			(
				DeleteFilesResults.f_GetResults()
				+ self(&CWebAppManagerActor::fp_SetupPrerequisites_UpdateAWSLambda, AWSCredentials)
			)
		;

		if (bDeleteFiles)
			DMibLogWithCategory(S3Upload, Info, "Deleting files and updating Lambda@Edge {fe2} s", Clock.f_GetTime());
		else
			DMibLogWithCategory(S3Upload, Info, "Updating Lambda@Edge {fe2} s", Clock.f_GetTime());
		Clock.f_Start();

		fg_Move(Results) | g_Unwrap;

		TCVector<CStr> PathsToInvalidate = {"/*"};
		if (!CloudFrontDistribution.f_IsEmpty())
		{
			DMibLogWithCategory(S3Upload, Info, "Invalidating CloudFront cache");
			co_await mp_CloudFrontActor(&CAwsCloudFrontActor::f_CreateInvalidation, CloudFrontDistribution, PathsToInvalidate);
			DMibLogWithCategory(S3Upload, Info, "Invalidating CloudFront cache {fe2} s", Clock.f_GetTime());
		}

		fg_Timeout(10.0) > [=]() mutable
			{
				DMibLogWithCategory(S3Upload, Info, "Invalidating CloudFront cache again");
				Clock.f_Start();
				mp_CloudFrontActor(&CAwsCloudFrontActor::f_CreateInvalidation, CloudFrontDistribution, PathsToInvalidate)
					> [=](TCAsyncResult<CStr> &&_Result)
					{
						if (!_Result)
							DMibLogWithCategory(S3Upload, Info, "Invalidating CloudFront cache failed: {}", _Result.f_GetExceptionStr());
						else
							DMibLogWithCategory(S3Upload, Info, "Invalidating CloudFront cache again {fe2} s", Clock.f_GetTime());
					}
				;
			}
		;

		co_await
			(
				g_Dispatch(*mp_FileActors) / [=]()
				{
					CFile::fs_WriteStringToFile(SourceCheckResults.m_ChecksumFile, SourceCheckResults.m_ChecksumStr, false);
					DMibLogWithCategory(S3Upload, Info, "Uploading static files to S3 took {fe2} s in total", GlobalClock.f_GetTime());
				}
			)
		;

		co_return {};
	}
}
