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
		uint32 gc_UpdateVersion = 35;
	}

	bool CWebAppManagerActor::fp_FormatAlternateSources(CStr &o_Str, TCVector<CWebAppManagerOptions::CPackage::CAlternateSource> const &_AlternateSources)
	{
		for (auto &AlternateSource : _AlternateSources)
		{
			CStr Destination = AlternateSource.m_Destination;
			if (Destination != "Default")
			{
				CStr AlternateSourceConfigName = "AlternateSource_{}"_f << Destination;
				Destination = fp_GetConfigValue(AlternateSourceConfigName, "").f_String();
				if (Destination.f_IsEmpty())
				{
					DMibLogWithCategory(WebAppManager, Error, "Missing alternate source in config file: {}", AlternateSourceConfigName);
					return false;
				}
			}
			o_Str += "	{{ pattern: new RegExp({}), destination: {}, isDefault: {} },\n"_f
				<< CJSON("^" + AlternateSource.m_Pattern + "$").f_ToString(nullptr)
				<< CJSON(Destination).f_ToString(nullptr)
				<< CJSON(Destination == "Default").f_ToString(nullptr)
			;
		}

		return true;
	}

	bool CWebAppManagerActor::fp_FormatAlternateSourcesSearchReplace(CStr &o_Str, TCVector<CWebAppManagerOptions::CPackage::CAlternateSource> const &_AlternateSources)
	{
		for (auto &AlternateSource : _AlternateSources)
		{
			if (AlternateSource.m_Search.f_IsEmpty() || AlternateSource.m_Destination == "Default")
				continue;

			CStr AlternateSourceConfigName = "AlternateSource_{}"_f << AlternateSource.m_Destination;
			CStr Destination = fp_GetConfigValue(AlternateSourceConfigName, "").f_String();
			if (Destination.f_IsEmpty())
			{
				DMibLogWithCategory(WebAppManager, Error, "Missing alternate source in config file: {}", AlternateSourceConfigName);
				return false;
			}

			o_Str += "	{{ pattern: new RegExp({}), destination: {}, search: new RegExp(escapeRegExp({}), \"g\"), replace: {} },\n"_f
				<< CJSON("^" + AlternateSource.m_Pattern + "$").f_ToString(nullptr)
				<< CJSON(Destination).f_ToString(nullptr)
				<< CJSON(AlternateSource.m_Search.f_Replace("{DomainName}", mp_Domain)).f_ToString(nullptr)
				<< CJSON(AlternateSource.m_Replace.f_Replace("{DomainName}", mp_Domain)).f_ToString(nullptr)
			;
		}

		return true;
	}

	TCFuture<void> CWebAppManagerActor::fp_SetupPrerequisites_UpdateAWSLambda(CAwsCredentials const &_AWSCredentials, CStr const &_Prefix)
	{
		auto OnResume = g_OnResume / [&]
			{
				if (f_IsDestroyed())
					DMibError("Shutting down");
			}
		;

		TCVector<CWebAppManagerOptions::CPackage::CRedirect> RedirectsTemporary;
		TCVector<CWebAppManagerOptions::CPackage::CRedirect> RedirectsPermanent;
		TCVector<CWebAppManagerOptions::CPackage::CAlternateSource> AlternateSources;

		for (auto &Package : mp_Options.m_Packages)
		{
			if (!Package.m_bUploadS3)
				continue;

			if (Package.m_UploadS3Prefix != _Prefix)
				continue;

			CStr ServerName = fp_GetPackageHostname(Package.f_GetName(), EHostnamePrefix_None);
			bool bIsMainServer = ServerName == mp_Domain;

			if (!bIsMainServer && Package.m_StaticPath.f_IsEmpty() && Package.m_UploadS3Prefix.f_IsEmpty())
				continue;

			AlternateSources.f_Insert(Package.m_AlternateSources);

			RedirectsTemporary.f_Insert(Package.m_RedirectsTemporary);
			RedirectsPermanent.f_Insert(Package.m_RedirectsPermanent);
		}

		CStr CloudFrontDistribution;
		CStr ConfigName = _Prefix ? CStr("AWSCloudFrontDistribution_{}"_f << _Prefix) : CStr("AWSCloudFrontDistribution");
		CloudFrontDistribution = fp_GetConfigValue(ConfigName, "").f_String();

		CStr FullDomainName = mp_Domain;
		if (_Prefix)
			FullDomainName = "{}.{}"_f << _Prefix << mp_Domain;

		CStr AWSLambdaRole = fp_GetConfigValue("AWSLambdaRole", "").f_String();

		if (CloudFrontDistribution.f_IsEmpty())
		{
			DMibLogWithCategory(WebAppManager, Warning, "{} value not specified in config, skipping Lambda creation", ConfigName);
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

		CStr PrefixLambdaSuffix = _Prefix ? CStr(".{}"_f << _Prefix) : CStr();

		CStr OriginRequestFunctionName = CStr("originrequest.{}{}{}"_f << mp_Options.m_S3BucketPrefix << mp_Domain << PrefixLambdaSuffix).f_ReplaceChar('.', '_');
		TCMap<CStr, CStr> OriginRequestFiles;
		CAwsLambdaActor::CFunctionConfiguration OriginRequestConfig;

		{
			OriginRequestConfig.m_Handler = "index.handler";
			OriginRequestConfig.m_Runtime = "nodejs12.x";
			OriginRequestConfig.m_Role = AWSLambdaRole;
			OriginRequestConfig.m_MemorySizeMB = 128;
			OriginRequestConfig.m_TimeoutSeconds = 3;
			OriginRequestConfig.m_bPublish = true;

			CStr AlternateSourcesString;
			if (!fp_FormatAlternateSources(AlternateSourcesString, AlternateSources))
				co_return {};

			CStr RequestHandler = CStr(R"----(
'use strict';
const alternateSources = [
{AlternateSources}];
const dotRegex = /\/(.*\/)*.*\..*/;
const slashEndRegex = /\/$/;
const startRegex = /$/;
const requestPrefix = {RequestPrefix};

exports.handler = (event, context, callback) => {
	let request = event.Records[0].cf.request;

	let uriWithoutPrefix = request.uri;
	if (uriWithoutPrefix.startsWith(requestPrefix))
		uriWithoutPrefix = uriWithoutPrefix.substr(requestPrefix.length);

	for (let alternate of alternateSources) {
		if (alternate.pattern.test(uriWithoutPrefix)) {
			if (!alternate.isDefault) {
				request.origin = {
					custom: {
						domainName: alternate.destination,
						port: 443,
						protocol: "https",
						path: "",
						sslProtocols: ["TLSv1.2"],
						readTimeout: 5,
						keepaliveTimeout: 5,
						customHeaders: {}
					}
				};
				request.headers["host"] = [{ key: "host", value: alternate.destination }];
				request.uri = uriWithoutPrefix;

				return callback(null, request);
			}
			break;
		}
	}

	let olduri = request.uri;

	if (dotRegex.test(olduri))
		return callback(null, request); // Something with . in name

	// Match any '/' that occurs at the end of a URI. Replace it with a default index
	let newuri;
	if (slashEndRegex.test(olduri))
		newuri = olduri.replace(slashEndRegex, "\/index.html");
	else
		newuri = olduri.replace(startRegex, "\/index.html");

	// Replace the received URI with the URI that includes the index page
	request.uri = newuri;

	// Return to CloudFront
	return callback(null, request);
};
)----")
				.f_Replace("{AlternateSources}", AlternateSourcesString)
				.f_Replace("{RequestPrefix}", CJSON(CStr("/{}"_f << _Prefix)).f_ToString(nullptr))
			;

			OriginRequestFiles["index.js"] = RequestHandler;
		}

		CStr OriginResponseFunctionName = CStr("originresponse.{}{}{}"_f << mp_Options.m_S3BucketPrefix << mp_Domain << PrefixLambdaSuffix).f_ReplaceChar('.', '_');
		TCMap<CStr, CStr> OriginResponseFiles;
		CAwsLambdaActor::CFunctionConfiguration OriginResponseConfig;

		{
			OriginResponseConfig.m_Handler = "index.handler";
			OriginResponseConfig.m_Runtime = "nodejs12.x";
			OriginResponseConfig.m_Role = AWSLambdaRole;
			OriginResponseConfig.m_MemorySizeMB = 128;
			OriginResponseConfig.m_TimeoutSeconds = 3;
			OriginResponseConfig.m_bPublish = true;

			CStr AlternateSourcesString;
			if (!fp_FormatAlternateSourcesSearchReplace(AlternateSourcesString, AlternateSources))
				co_return {};

			CStr ContentSecurityPolicy = "default-src 'none' {};"_f << mp_Options.m_ContentSecurity_DefaultSrc;
			ContentSecurityPolicy += " img-src 'self' data: *.{0} {0} {1} ;"_f << mp_Domain << mp_Options.m_ContentSecurity_ImgSrc;
			ContentSecurityPolicy += " font-src 'self' data: *.{0} {0} {1} ;"_f << mp_Domain << mp_Options.m_ContentSecurity_FontSrc;
			ContentSecurityPolicy += " media-src 'self' *.{0} {0} {1} ;"_f << mp_Domain << mp_Options.m_ContentSecurity_MediaSrc;
			ContentSecurityPolicy += " script-src 'self' *.{0} {0} {1};"_f << mp_Domain << mp_Options.m_ContentSecurity_ScriptSrc;
			ContentSecurityPolicy += " style-src 'self' *.{0} {0} {1} ;"_f << mp_Domain << mp_Options.m_ContentSecurity_StyleSrc;
			ContentSecurityPolicy += " frame-src 'self' *.{0} {0} {1} ;"_f << mp_Domain << mp_Options.m_ContentSecurity_FrameSrc;
			ContentSecurityPolicy += " connect-src 'self' *.{0} {0} {1} ;"_f << mp_Domain << mp_Options.m_ContentSecurity_ConnectSrc;
			ContentSecurityPolicy += " child-src 'self' *.{0} {0} {1} ;"_f << mp_Domain << mp_Options.m_ContentSecurity_ChildSrc;
			ContentSecurityPolicy += " form-action 'self' *.{0} {0} {1} ;"_f << mp_Domain << mp_Options.m_ContentSecurity_FormAction;
			ContentSecurityPolicy += " manifest-action 'self' *.{0} {0} {1} ;"_f << mp_Domain << mp_Options.m_ContentSecurity_ManifestSrc;
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


			CStr RedirectContents;

			for (auto &Redirect : RedirectsTemporary)
			{
				RedirectContents +=
					"    if (uri.startsWith({}))\n"
					"		return doRedirect({}, true);\n"_f
					<< NEncoding::CJSON(Redirect.m_From).f_ToString(nullptr)
					<< NEncoding::CJSON(Redirect.m_To.f_Replace("{DomainName}", mp_Domain)).f_ToString(nullptr)
				;
			}

			for (auto &Redirect : RedirectsPermanent)
			{
				RedirectContents +=
					"    if (uri.startsWith({}))\n"
					"		return doRedirect({}, false);\n"_f
					<< NEncoding::CJSON(Redirect.m_From).f_ToString(nullptr)
					<< NEncoding::CJSON(Redirect.m_To.f_Replace("{DomainName}", mp_Domain)).f_ToString(nullptr)
				;
			}

			CStr AllowRedirectsOutsideOfDomainPatternsString;

			for (auto &Pattern : mp_Options.m_AllowRedirectsOutsideOfDomainPatterns)
				AllowRedirectsOutsideOfDomainPatternsString += "	new RegExp({}),\n"_f << CJSON(Pattern.f_Replace("{DomainName}", mp_Domain)).f_ToString(nullptr);

			OriginResponseFiles["index.js"] = CStr(R"----(
'use strict';
function escapeRegExp(string) {
	return string.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

let alternateSources = [
{AlternateSources}];

let allowRedirectsOutsideOfDomainPatterns = [
{AllowRedirectsOutsideOfDomainPatterns}];

const domainRegex = /(http(s)?:\/\/)(([a-zA-Z\d-]+\.?)+)(\/.*)/;

const getContent = function(url) {
	return new Promise((resolve, reject) => {
		const lib = url.startsWith('https') ? require('https') : require('http');
		const request = lib.get(url, (response) => {
			if (response.statusCode < 200 || response.statusCode > 299) {
				reject(new Error('Failed to load page, status code: ' + response.statusCode));
			}
			const body = [];
			response.on('data', (chunk) => body.push(chunk));
			response.on('end', () => resolve(body.join('')));
		});
		request.on('error', (err) => reject(err))
	})
};

function allowedByPatterns(domain) {
	for (let pattern of allowRedirectsOutsideOfDomainPatterns) {
		if (pattern.test(domain))
			return true;
	}

	return false;
}

exports.handler = async (event) => {

	// Get contents of response
	const request = event.Records[0].cf.request;
	const response = event.Records[0].cf.response;
	const headers = response.headers;

	// Set new headers
	headers['strict-transport-security'] = [{key: 'Strict-Transport-Security', value: 'max-age=63072000; includeSubdomains; preload'}];
	headers['content-security-policy'] = [{key: 'Content-Security-Policy', value: "{ContentSecurityPolicy}"}];
	headers['x-content-type-options'] = [{key: 'X-Content-Type-Options', value: 'nosniff'}];
	headers['x-frame-options'] = [{key: 'X-Frame-Options', value: 'DENY'}];
	headers['x-xss-protection'] = [{key: 'X-XSS-Protection', value: '1; mode=block'}];
	headers['referrer-policy'] = [{key: 'Referrer-Policy', value: 'same-origin'}];

	if (!{AllowRedirectsOutsideOfDomain} && (response.status == 301 || response.status == 302) && headers['location'] && headers['location'][0] && headers['location'][0].value) {
		let matchResult = headers['location'][0].value.match(domainRegex);
		if (matchResult && matchResult[3] && matchResult[3] != {DomainName} && !matchResult[3].endsWith({DomainSuffix}) && !allowedByPatterns(matchResult[3])) {
			headers['location'][0].value = matchResult[1] + {FullDomainName} + matchResult[5];
		}
	}

{AccessControl}
	function doRedirect(redirectTo, temporary) {
		headers['location'] = [
			{
				key: 'Location',
				value: redirectTo.replace("{}", request.uri.replace(/\/index.html$/, '')),
			},
		];

		response.body = '';

		if (temporary) {
			response.status = 302;
			response.statusDescription = 'Found';
		} else {
			response.status = 301;
			response.statusDescription = 'Moved Permanently';
		}

		return response;
	}

	var uri = request.uri;
{Redirects}

	if (response.status == 200) {
		for (let alternate of alternateSources) {
			if (alternate.pattern.test(request.uri)) {
				let originalBody = await getContent("https://" + alternate.destination + request.uri);
				response.body = originalBody.replace(alternate.search, alternate.replace);
				break;
			}
		}
	}

	//Return modified response
	return response;
};
)----")
	.f_Replace("{ContentSecurityPolicy}", ContentSecurityPolicy)
	.f_Replace("{AlternateSources}", AlternateSourcesString)
	.f_Replace("{AccessControl}", AccessControl)
	.f_Replace("{Redirects}", RedirectContents)
	.f_Replace("{AllowRedirectsOutsideOfDomain}", mp_Options.m_bAllowRedirectsOutsideOfDomain ? "true" : "false")
	.f_Replace("{AllowRedirectsOutsideOfDomainPatterns}", AllowRedirectsOutsideOfDomainPatternsString)
	.f_Replace("{DomainName}", NEncoding::CJSON(mp_Domain).f_ToString(nullptr))
	.f_Replace("{FullDomainName}", NEncoding::CJSON(FullDomainName).f_ToString(nullptr))
	.f_Replace("{DomainSuffix}", NEncoding::CJSON(".{}"_f << mp_Domain).f_ToString(nullptr))

;
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

		NContainer::TCMap<CAwsCloudFrontActor::EFunctionEventType, NStr::CStr> FunctionAssociations;
		FunctionAssociations[CAwsCloudFrontActor::EFunctionEventType_OriginRequest] = OriginRequestInfo.m_Arn;
		FunctionAssociations[CAwsCloudFrontActor::EFunctionEventType_OriginResponse] = OriginResponseInfo.m_Arn;

		if (!RedirectsTemporary.f_IsEmpty() || !RedirectsPermanent.f_IsEmpty())
		{
			CStr ViewerResponseFunctionName = CStr("viewerresponse.{}{}{}"_f << mp_Options.m_S3BucketPrefix << mp_Domain << PrefixLambdaSuffix).f_ReplaceChar('.', '_');
			TCMap<CStr, CStr> ViewerResponseFiles;
			CAwsLambdaActor::CFunctionConfiguration ViewerResponseConfig;

			{
				ViewerResponseConfig.m_Handler = "index.handler";
				ViewerResponseConfig.m_Runtime = "nodejs12.x";
				ViewerResponseConfig.m_Role = AWSLambdaRole;
				ViewerResponseConfig.m_MemorySizeMB = 128;
				ViewerResponseConfig.m_TimeoutSeconds = 3;
				ViewerResponseConfig.m_bPublish = true;

				ViewerResponseFiles["index.js"] = CStr(R"----(
	'use strict';
	exports.handler = (event, context, callback) => {

		// Get contents of response
		const request = event.Records[0].cf.request;
		const response = event.Records[0].cf.response;
		const headers = response.headers;

		// Set new headers
		if ((response.status == 302 || response.status == 301) && headers['location'] && request.querystring)
			headers['location'][0].value = headers['location'][0].value + "?" + request.querystring;

		//Return modified response
		callback(null, response);
	};
)----");
			}

			auto ViewerResponseInfo = co_await
				(
					mp_LambdaActor(&CAwsLambdaActor::f_CreateOrUpdateFunction, ViewerResponseFunctionName, ViewerResponseFiles, ViewerResponseConfig)
					% "Update AWS Lambda viewer response function"
				)
			;

			FunctionAssociations[CAwsCloudFrontActor::EFunctionEventType_ViewerResponse] = ViewerResponseInfo.m_Arn;
		}

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
		TCSet<CStr> UploadPrefixes;
		TCSet<CStr> CloudFrontDistributions;

		TCVector<CWebAppManagerOptions::CPackage::CRedirect> RedirectsTemporary;
		TCVector<CWebAppManagerOptions::CPackage::CRedirect> RedirectsPermanent;
		TCVector<CWebAppManagerOptions::CPackage::CAlternateSource> AlternateSources;

		for (auto &Package : mp_Options.m_Packages)
		{
			if (!Package.m_bUploadS3)
				continue;

			CStr ServerName = fp_GetPackageHostname(Package.f_GetName(), EHostnamePrefix_None);
			bool bIsMainServer = ServerName == mp_Domain;

			if (!bIsMainServer && Package.m_StaticPath.f_IsEmpty() && Package.m_UploadS3Prefix.f_IsEmpty())
				continue;

			AlternateSources.f_Insert(Package.m_AlternateSources);

			RedirectsTemporary.f_Insert(Package.m_RedirectsTemporary);
			RedirectsPermanent.f_Insert(Package.m_RedirectsPermanent);

			CStr StaticPath;

			if (!bIsMainServer)
				StaticPath = Package.m_StaticPath;

			if (Package.m_UploadS3Prefix)
				StaticPath = CFile::fs_AppendPath(Package.m_UploadS3Prefix, StaticPath);

			UploadPrefixes[Package.m_UploadS3Prefix];

			{
				CStr ConfigName = Package.m_UploadS3Prefix ? CStr("AWSCloudFrontDistribution_{}"_f << Package.m_UploadS3Prefix) : CStr("AWSCloudFrontDistribution");
				auto Distribution = fp_GetConfigValue(ConfigName, "").f_String();
				if (Distribution)
					CloudFrontDistributions[Distribution];
			}

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

			if (bIsMainServer && Package.m_bAllowRobots)
				bAllowRobots = true;
		}

		CStr AllowRobotsText = fp_GetAllowRobots((mp_Options.m_bAllowRobots || bAllowRobots) && mp_bAllowRobots);

		if (ManifestConfig.m_IncludeWildcards.f_IsEmpty())
			co_return {};

		DMibLogWithCategory(S3Upload, Info, "Uploading static files to S3");
		NTime::CClock GlobalClock{true};

		CAwsCredentials AWSCredentials;

		AWSCredentials.m_Region = fp_GetConfigValue("AWSS3Region", "").f_String();
		AWSCredentials.m_AccessKeyID = fp_GetConfigValue("AWSAccessKeyID", "").f_String();
		AWSCredentials.m_SecretKey = fp_GetConfigValue("AWSSecretKey", "").f_String();

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

		CStr AlternateSourcesString;
		if (!fp_FormatAlternateSources(AlternateSourcesString, AlternateSources))
			co_return {};

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

						for (auto &Prefix : UploadPrefixes)
						{
							CStr RobotsPath = Prefix / "robots.txt";
							if (!DirectoryManifest.m_Files.f_FindEqual(RobotsPath))
							{
								auto &ManifestFile = DirectoryManifest.m_Files[RobotsPath];
								ManifestFile.m_SymlinkData = AllowRobotsText;
								ManifestFile.m_Digest = CHash_SHA256::fs_DigestFromData(ManifestFile.m_SymlinkData.f_GetStr(), ManifestFile.m_SymlinkData.f_GetLen());
							}
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
					Stream << CloudFrontDistributions;
					Stream << Options.m_ContentSecurity_DefaultSrc;
					Stream << Options.m_ContentSecurity_ManifestSrc;
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
					Stream << AlternateSources;
					Stream << AlternateSourcesString;
					Stream << RedirectsTemporary;
					Stream << RedirectsPermanent;
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

							if (CFile::fs_GetFile(FileName) == "robots.txt" && !File.m_SymlinkData.f_IsEmpty())
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

		if (!mp_CurlActors.f_IsConstructed())
			mp_CurlActors.f_Construct(fg_Construct(fg_Construct(), "S3/CloudFront curl actor"));

		mp_LastCloudFrontDistributions = CloudFrontDistributions;
		mp_CloudFrontActor = fg_Construct(*mp_CurlActors, AWSCredentials);

		if (SourceCheckResults.m_bUpToDate)
		{
			DMibLogWithCategory(S3Upload, Info, "S3 files were already up to date");
			co_return {};
		}

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

						if (CFile::fs_GetFile(FileName) == "robots.txt" && !NewFile.m_SymlinkData.f_IsEmpty())
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

		TCActorResultVector<void> LambdaUpdateResultsVector;
		for (auto &UploadPrefix : UploadPrefixes)
			self(&CWebAppManagerActor::fp_SetupPrerequisites_UpdateAWSLambda, AWSCredentials, UploadPrefix) > LambdaUpdateResultsVector.f_AddResult();

		auto [Results, LambdaUpdateResults] = co_await
			(
				DeleteFilesResults.f_GetResults()
				+ LambdaUpdateResultsVector.f_GetResults()
			)
		;

		if (bDeleteFiles)
			DMibLogWithCategory(S3Upload, Info, "Deleting files and updating Lambda@Edge {fe2} s", Clock.f_GetTime());
		else
			DMibLogWithCategory(S3Upload, Info, "Updating Lambda@Edge {fe2} s", Clock.f_GetTime());
		Clock.f_Start();

		fg_Move(Results) | g_Unwrap;
		fg_Move(LambdaUpdateResults) | g_Unwrap;

		auto fInvalidateCache = g_ActorFunctor / [this, CloudFrontDistributions]() -> TCFuture<void>
			{
				auto OnResume = g_OnResume / [&]
					{
						if (f_IsDestroyed())
							DMibError("Shutting down");
					}
				;

				TCVector<CStr> PathsToInvalidate = {"/*"};
				TCSet<CStr> ToInvalidate = CloudFrontDistributions;

				TCVector<TCAsyncResult<CStr>> Errors;

				for (mint iRetry = 10; iRetry >= 0; --iRetry)
				{
					TCActorResultMap<CStr, CStr> Results;
					for (auto &Distribution : ToInvalidate)
						mp_CloudFrontActor(&CAwsCloudFrontActor::f_CreateInvalidation, Distribution, PathsToInvalidate) > Results.f_AddResult(Distribution);

					auto ResultsMap = co_await Results.f_GetResults();

					if (iRetry == 0)
					{
						fg_Move(ResultsMap) | g_Unwrap;
						break;
					}

					for (auto &Result : ResultsMap)
					{
						auto &Key = ResultsMap.fs_GetKey(Result);

						if (!Result && Result.f_GetExceptionStr().f_Find("503: ServiceUnavailable") >= 0)
							continue;

						if (!Result)
							Errors.f_Insert(fg_Move(Result));

						ToInvalidate.f_Remove(Key);
					}

					if (ToInvalidate.f_IsEmpty())
						break;

					co_await fg_Timeout(60.0);

					DMibLogWithCategory(S3Upload, Info, "Invalidating CloudFront cache retry due to 503: ServiceUnavailable for: {vs}", ToInvalidate);
				}

				fg_Move(Errors) | g_Unwrap;

				co_return {};
			}
		;

		DMibLogWithCategory(S3Upload, Info, "Invalidating CloudFront cache");
		co_await fInvalidateCache();
		DMibLogWithCategory(S3Upload, Info, "Invalidating CloudFront cache {fe2} s", Clock.f_GetTime());

		fg_Timeout(60.0) > [fInvalidateCache = fg_Move(fInvalidateCache), Clock]() mutable
			{
				Clock.f_Start();
 				DMibLogWithCategory(S3Upload, Info, "Invalidating CloudFront cache again");
				fInvalidateCache() > [=](TCAsyncResult<void> &&_Result)
					{
						if (!_Result)
						{
							DMibLogWithCategory(S3Upload, Info, "Invalidating CloudFront cache again failed: {}", _Result.f_GetExceptionStr());
							return;
						}
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

	TCFuture<void> CWebAppManagerActor::f_InvalidateCloudFrontCaches()
	{
		co_await fp_InvalidateCloudfrontDistributions();

		co_return {};
	}

	TCFuture<void> CWebAppManagerActor::fp_InvalidateCloudfrontDistributions()
	{
		if (mp_LastCloudFrontDistributions.f_IsEmpty())
			co_return {};

		if (!mp_bInitialS3UploadDone)
			co_return {};

		co_await
			(
				mp_S3UploadSequencer / [=]() -> TCFuture<void>
				{
					NTime::CClock Clock{true};

					TCVector<CStr> PathsToInvalidate = {"/*"};

					DMibLogWithCategory(S3Upload, Info, "Invalidating CloudFront cache out of band");
					TCActorResultVector<CStr> Results;
					for (auto &Distribution : mp_LastCloudFrontDistributions)
						mp_CloudFrontActor(&CAwsCloudFrontActor::f_CreateInvalidation, Distribution, PathsToInvalidate) > Results.f_AddResult();
					co_await Results.f_GetResults() | g_Unwrap;
					DMibLogWithCategory(S3Upload, Info, "Invalidating CloudFront cache {fe2} s", Clock.f_GetTime());

					co_return {};
				}
			)
		;

		co_return {};
	}
}
