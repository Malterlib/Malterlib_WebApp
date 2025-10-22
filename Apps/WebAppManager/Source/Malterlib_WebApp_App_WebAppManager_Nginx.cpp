
#include "Malterlib_WebApp_App_WebAppManager_Server.h"

#include <Mib/Web/HTTP/URL>
#include <Mib/Cryptography/Certificate>
#include <Mib/Concurrency/Actor/Timer>
#include <Mib/Concurrency/LogError>

namespace NMib::NWebApp::NWebAppManager
{
	mint CWebAppManagerActor::fs_GetNginxWorkerFileLimits()
	{
		mint nFilesPerConnection = 2; // nginx incoming + nginx proxy -> node

		return 65536*nFilesPerConnection + 8192;
	}

	mint CWebAppManagerActor::fs_GetNginxFileLimits(mint _nNodes)
	{
		return fs_GetNginxWorkerFileLimits() * _nNodes + 8192;
	}

ch8 const *g_pCheckServerName = R"---(
		set $invalid_host "true";

		if ($host ~ "^{DomainNameEscaped}$" )
		{
			set $invalid_host "false";
		}

		if ($host ~ "^[a-z0-9-]*\.{DomainNameEscaped}$" )
		{
			set $invalid_host "false";
		}

		if ($invalid_host = "true")
		{
			return 444;
		}
)---";

ch8 const *g_pServerTemplate[2] =
	{
R"---(
{AlternateSources}

		location ~ "^/{SubPath}/([A-Za-z0-9]{40}\.(css|js))$"
		{
			gzip_static {GZipStatic};
			expires max;
{SecurityHeaders}
			add_header Cache-Control public;
			alias "{StaticRoot}/$1";
			access_log logs/static_access_{PackageName}.log;
		}

		# pass all other requests to upstream
		location ~ ^/{SubPath}({DefaultLocation})?$
		{
{PathRedirect}
{ServerRootOptions_{PackageName}}
{DefaultSourceSubFilters}
			error_page 502 = @{PackageName}_Fallback$1$is_args$args;
			proxy_pass http://{UpstreamSticky}$1$is_args$args;
			proxy_http_version 1.1;
			proxy_set_header Host $host;
			proxy_set_header Upgrade $http_upgrade; # allow websockets
			proxy_set_header Connection $connection_upgrade;
			proxy_set_header X-Forwarded-For $remote_addr; # preserve client IP
			proxy_buffer_size 128k;
			proxy_buffers 4 256k;
			proxy_busy_buffers_size 256k;
		}
		location @{PackageName}_Fallback
		{
{PathRedirect}
{ServerRootOptions_{PackageName}}
{DefaultSourceSubFilters}
			proxy_pass http://{Upstream};
			proxy_http_version 1.1;
			proxy_set_header Host $host;
			proxy_set_header Upgrade $http_upgrade; # allow websockets
			proxy_set_header Connection $connection_upgrade;
			proxy_set_header X-Forwarded-For $remote_addr; # preserve client IP
			proxy_buffer_size 128k;
			proxy_buffers 4 256k;
			proxy_busy_buffers_size 256k;
		}
)---"
, R"---(
	server
	{
		listen {BindTo}{SSLPort} {DefaultServer};
		{DisableIPV6} listen [::]:{SSLPort} {DefaultServer};
		http2  on;
		server_name {ServerName} {ServerNameExtra_{PackageName}};
		access_log logs/access_{PackageName}.log upstreamlog;
		client_max_body_size 10M;

{CheckServerNameLogic}

{ServerAccessCheck_{PackageName}}

		# If your application is not compatible with IE <= 10, this will redirect visitors to a page advising a browser update
		# This works because IE 11 does not present itself as MSIE anymore
		if ($http_user_agent ~ "MSIE" )
		{
			return 303 https://browser-update.org/update.html;
		}

{ServerRedirect_{PackageName}}
{CustomizationInServer_{PackageName}}

{StaticPackages}
{SubPackages}

		location ~ "{StaticUriRegex}"
		{
			gzip_static {GZipStatic};
			expires max;
{SecurityHeaders}
			add_header Cache-Control public;
			try_files "$uri" "$uri/index.html" "/index.html";

			alias "{StaticRoot}";
			access_log logs/static_access_{PackageName}.log;
		}

		location ~ ^/robots.txt$ {
			return 200 "{AllowRobots}";
		}

		# pass all other requests to upstream
		location ~ (^{DefaultLocation})$
		{
{PathRedirect}
{ServerRootOptions_{PackageName}}
{DefaultSourceSubFilters}
			error_page 502 = @{PackageName}_Fallback$1$is_args$args;
			proxy_pass http://{UpstreamSticky}$1$is_args$args;
			proxy_http_version 1.1;
			proxy_set_header Host $host;
			proxy_set_header Upgrade $http_upgrade; # allow websockets
			proxy_set_header Connection $connection_upgrade;
			proxy_set_header X-Forwarded-For $remote_addr; # preserve client IP
			proxy_buffer_size 128k;
			proxy_buffers 4 256k;
			proxy_busy_buffers_size 256k;
		}
		location @{PackageName}_Fallback
		{
{PathRedirect}
{ServerRootOptions_{PackageName}}
{DefaultSourceSubFilters}
			proxy_pass http://{Upstream};
			proxy_http_version 1.1;
			proxy_set_header Host $host;
			proxy_set_header Upgrade $http_upgrade; # allow websockets
			proxy_set_header Connection $connection_upgrade;
			proxy_set_header X-Forwarded-For $remote_addr; # preserve client IP
			proxy_buffer_size 128k;
			proxy_buffers 4 256k;
			proxy_busy_buffers_size 256k;
		}
{AlternateSources}
	}
)---"
	}
;

ch8 const *g_pStaticServerTemplate[2] =
	{
R"---(
		location ~ ^/{SubPath}({DefaultLocation})?$
		{
{PathRedirect}
{DefaultSourceSubFilters}
			gzip_static {GZipStatic};
{SecurityHeaders}
			add_header Cache-Control no-cache;
			alias "{StaticRoot}";
			try_files "$1" "$1/index.html" "/index.html";
			access_log logs/static_access_{PackageName}.log;
			error_page 403 = /403/index.html;
			error_page 404 = /404/index.html;
		}

{AlternateSources}
)---"
, R"---(
	server
	{
		listen {BindTo}{SSLPort} {DefaultServer};
		{DisableIPV6} listen [::]:{SSLPort} {DefaultServer};
		http2  on;
		server_name {ServerName} {ServerNameExtra_{PackageName}};
		access_log logs/access_{PackageName}.log upstreamlog;
		client_max_body_size 10M;

{CheckServerNameLogic}

{ServerAccessCheck_{PackageName}}

		# If your application is not compatible with IE <= 10, this will redirect visitors to a page advising a browser update
		# This works because IE 11 does not present itself as MSIE anymore
		if ($http_user_agent ~ "MSIE" )
		{
			return 303 https://browser-update.org/update.html;
		}

{ServerRedirect_{PackageName}}
{CustomizationInServer_{PackageName}}

{StaticPackages}
{SubPackages}

		location ~ ^/robots.txt$ {
			return 200 "{AllowRobots}";
		}

		location ~ ^({DefaultLocation})$
		{
{PathRedirect}
{DefaultSourceSubFilters}
			gzip_static {GZipStatic};
{SecurityHeaders}
			add_header Cache-Control no-cache;
			alias "{StaticRoot}";
			try_files "$1" "$1/index.html" "/index.html";
			access_log logs/static_access_{PackageName}.log;
			error_page 403 = /403/index.html;
			error_page 404 = /404/index.html;
		}

{AlternateSources}
	}
)---"
	}
;

ch8 const *g_pFastCGIServerTemplate[2] =
	{
R"---(
		location ~ "^/{SubPath}(/[A-Za-z0-9]{40}\.(css|js))$"
		{
			gzip_static {GZipStatic};
			expires max;
{SecurityHeaders}
			add_header Cache-Control public;
			alias "{StaticRoot}$1";
			access_log logs/static_access_{PackageName}.log;
		}

		# pass all requests to FastCGI
		location ~ ^/{SubPath}({DefaultLocation})?$
		{
{PathRedirect}
{ServerRootOptions_{PackageName}}
{DefaultSourceSubFilters}
			include {FastCGIFile};

			error_page 502 = @{PackageName}_Fallback;

			fastcgi_pass {UpstreamSticky};
			fastcgi_keep_conn on;

			gzip off;
		}
		location @{PackageName}_Fallback
		{
{PathRedirect}
{ServerRootOptions_{PackageName}}
{DefaultSourceSubFilters}
			include {FastCGIFile};

			fastcgi_pass {Upstream};
			fastcgi_keep_conn on;

			gzip off;
		}

{AlternateSources}
)---"
, R"---(
	server
	{
		listen {BindTo}{SSLPort} {DefaultServer};
		{DisableIPV6} listen [::]:{SSLPort} {DefaultServer};
		http2  on;
		server_name {ServerName} {ServerNameExtra_{PackageName}};
		access_log logs/access_{PackageName}.log upstreamlog;
		client_max_body_size 10M;

{CheckServerNameLogic}

{ServerAccessCheck_{PackageName}}

		# If your application is not compatible with IE <= 10, this will redirect visitors to a page advising a browser update
		# This works because IE 11 does not present itself as MSIE anymore
		if ($http_user_agent ~ "MSIE" )
		{
			return 303 https://browser-update.org/update.html;
		}

{ServerRedirect_{PackageName}}
{CustomizationInServer_{PackageName}}

{StaticPackages}
{SubPackages}

		location ~ "{StaticUriRegex}"
		{
			gzip_static {GZipStatic};
			expires max;
{SecurityHeaders}
			add_header Cache-Control public;
			try_files "$uri" "$uri/index.html" "/index.html";

			alias "{StaticRoot}";
			access_log logs/static_access_{PackageName}.log;
		}

		location ~ ^/robots.txt$ {
			return 200 "{AllowRobots}";
		}

		# pass all requests to FastCGI
		location ~ ^({DefaultLocation})$
		{
{PathRedirect}
{ServerRootOptions_{PackageName}}
{DefaultSourceSubFilters}
			include {FastCGIFile};

			error_page 502 = @{PackageName}_Fallback;

			fastcgi_pass {UpstreamSticky};
			fastcgi_keep_conn on;

			gzip off;
		}
		location @{PackageName}_Fallback
		{
{PathRedirect}
{ServerRootOptions_{PackageName}}
{DefaultSourceSubFilters}
			include {FastCGIFile};

			fastcgi_pass {Upstream};
			fastcgi_keep_conn on;

			gzip off;
		}

{AlternateSources}
	}
)---"
	}
;

ch8 const *g_pWebsocketServerTemplate[2] =
	{
R"---(
		# pass all requests to Websocket
		location ~ ^/{SubPath}({DefaultLocation})?$
		{
{PathRedirect}
{ServerRootOptions_{PackageName}}
{DefaultSourceSubFilters}
			error_page 502 = @{PackageName}_Fallback;
			proxy_pass http://{UpstreamSticky};
			proxy_http_version 1.1;
			proxy_set_header Host $host;
			proxy_set_header Upgrade $http_upgrade; # allow websockets
			proxy_set_header Connection $connection_upgrade;
			proxy_set_header X-Forwarded-For $remote_addr; # preserve client IP
			proxy_buffer_size 128k;
			proxy_buffers 4 256k;
			proxy_busy_buffers_size 256k;
		}
		location @{PackageName}_Fallback
		{
{PathRedirect}
{ServerRootOptions_{PackageName}}
{DefaultSourceSubFilters}
			proxy_pass http://{Upstream};
			proxy_http_version 1.1;
			proxy_set_header Host $host;
			proxy_set_header Upgrade $http_upgrade; # allow websockets
			proxy_set_header Connection $connection_upgrade;
			proxy_set_header X-Forwarded-For $remote_addr; # preserve client IP
			proxy_buffer_size 128k;
			proxy_buffers 4 256k;
			proxy_busy_buffers_size 256k;
		}

{AlternateSources}
)---"
, R"---(
	server
	{
		listen {BindTo}{SSLPort} {DefaultServer};
		{DisableIPV6} listen [::]:{SSLPort} {DefaultServer};
		http2  on;
		server_name {ServerName} {ServerNameExtra_{PackageName}};
		access_log logs/access_{PackageName}.log upstreamlog;
		client_max_body_size 10M;

{CheckServerNameLogic}

{ServerAccessCheck_{PackageName}}

		# If your application is not compatible with IE <= 10, this will redirect visitors to a page advising a browser update
		# This works because IE 11 does not present itself as MSIE anymore
		if ($http_user_agent ~ "MSIE" )
		{
			return 303 https://browser-update.org/update.html;
		}

{ServerRedirect_{PackageName}}
{CustomizationInServer_{PackageName}}

{StaticPackages}
{SubPackages}

		location ~ ^/robots.txt$ {
			return 200 "{AllowRobots}";
		}

		# pass all requests to Websocket
		location ~ ^({DefaultLocation})$
		{
{PathRedirect}
{ServerRootOptions_{PackageName}}
{DefaultSourceSubFilters}
			error_page 502 = @{PackageName}_Fallback;
			proxy_pass http://{UpstreamSticky};
			proxy_http_version 1.1;
			proxy_set_header Host $host;
			proxy_set_header Upgrade $http_upgrade; # allow websockets
			proxy_set_header Connection $connection_upgrade;
			proxy_set_header X-Forwarded-For $remote_addr; # preserve client IP
			proxy_buffer_size 128k;
			proxy_buffers 4 256k;
			proxy_busy_buffers_size 256k;
		}
		location @{PackageName}_Fallback
		{
{PathRedirect}
{ServerRootOptions_{PackageName}}
{DefaultSourceSubFilters}
			proxy_pass http://{Upstream};
			proxy_http_version 1.1;
			proxy_set_header Host $host;
			proxy_set_header Upgrade $http_upgrade; # allow websockets
			proxy_set_header Connection $connection_upgrade;
			proxy_set_header X-Forwarded-For $remote_addr; # preserve client IP
			proxy_buffer_size 128k;
			proxy_buffers 4 256k;
			proxy_busy_buffers_size 256k;
		}

{AlternateSources}
	}
)---"
	}
;

ch8 const *g_pServerSeparateStaticRootTemplate = R"---(
	server
	{
		listen {BindTo}{SSLPort} {DefaultServer};
		{DisableIPV6} listen [::]:{SSLPort} {DefaultServer};
		http2  on;
		server_name {ServerNameStatic} {ServerNameStaticSource};
		access_log logs/static_access_{PackageName}.log;
		client_max_body_size 10M;

		add_header 'Access-Control-Allow-Origin' 'https://{ServerName}{SSLPortRewrite}';
{SecurityHeaders}
		add_header Cache-Control public;

		location ~ ^/robots.txt$ {
			return 200 "User-agent: *\\nDisallow: /";
		}

		location ~ "{StaticUriRegex}"
		{
			gzip_static {GZipStatic};
			expires max;
			try_files "$uri" "$uri/index.html" "/index.html";

			alias "{StaticRoot}";
		}

		location ~ "^/packages/.*\.(jpg|jpeg|png|gif|mp3|ico|pdf|svg|eot|woff|woff2|ttf|otf)$"
		{
			root "{StaticRoot}";
		}

		location ~ "\.(jpg|jpeg|png|gif|mp3|ico|pdf|svg|eot|woff|woff2|ttf|otf)$"
		{
			root "{StaticRoot}/app";
		}

{AlternateSources}
	}
)---";

	CStr CWebAppManagerActor::fp_GetAllowRobots(bool _bAllow)
	{
		if (!_bAllow)
		{
			return	"User-agent: *\n"
					"Disallow: /"
			;
		}

		CStr Text = "User-agent: *\n";

		for (auto &Disallow : mp_Options.m_RobotsDisallow)
			Text += "Disallow: {}\n"_f << Disallow;

		for (auto &Allow : mp_Options.m_RobotsAllow)
			Text += "Allow: {}\n"_f << Allow;

		if (mp_Options.m_RobotsSitemap)
			Text += "Sitemap: {}\n"_f << fp_DoCustomStringReplacements(mp_Options.m_RobotsSitemap.f_Replace("{DomainName}", mp_Domain));

		return Text;
	}

	TCFuture<void> CWebAppManagerActor::fp_SetupPrerequisites_NginxUser()
	{
		struct CResults
		{
			CUser m_User{"", ""};
#ifdef DPlatformFamily_Windows
			CStrSecure m_UserPassword;
#endif
		};

		CResults SetupResults;
		{
			auto BlockingActorCheckout = fg_BlockingActor();
			SetupResults = co_await
				(
					g_Dispatch(BlockingActorCheckout) / [User = mp_NginxUser, bRunningEleveated = mp_bRunningElevated]() mutable -> CResults
					{
						CResults Results;

						Results.m_User = User;
		#ifdef DPlatformFamily_Windows
						fsp_SetupUser(Results.m_User, bRunningEleveated, Results.m_UserPassword);
		#else
						fsp_SetupUser(Results.m_User, bRunningEleveated);
		#endif
						return Results;
					}
				)
			;
		}

		CStr ProgramDirectory = CFile::fs_GetProgramDirectory();

		mp_NginxUser = SetupResults.m_User;

		TCFuture<void> SavePasswordFuture;
#ifdef DPlatformFamily_Windows
		if (!SetupResults.m_UserPassword.f_IsEmpty())
			SavePasswordFuture = fp_SaveUserPassword(mp_NginxUser.m_UserName, SetupResults.m_UserPassword);
		else
#endif
			SavePasswordFuture = g_Void;

		co_await fg_Move(SavePasswordFuture);

		co_return {};
	}

	constexpr CStr gc_SubFilterConfig = gc_Str
		<
			"			sub_filter_once off;\n"
			"			sub_filter_types *;\n"
			"			proxy_set_header Accept-Encoding \"\";\n"
		>
	;

	TCFuture<void> CWebAppManagerActor::fp_SetupPrerequisites_Nginx()
	{
		struct CResults
		{
			CStr m_ConfigContents;
			TCMap<CStr, CStr> m_Redirects;
			CStr m_CertificateFile;
			CStr m_CertificateKeyFile;
			bool m_bHasDHParamFile = false;
		};

		struct CAlternateSource
		{
			CStr m_Locations;
			CStr m_DefaultLocation;
			CStr m_DefaultSourceSubFilters;
		};

		CStr NginxDirectory = fp_GetDataPath("nginx");
		CStr DhParamFile = NginxDirectory + "/certificates/dhparam.pem";
		CStr ConfigFile = NginxDirectory + "/nginx.conf";
		CStr FastCGIFile = NginxDirectory + "/FastCGI.conf";
		bool bEnableSeparateStaticRoot = fp_GetConfigValue("EnableSeparateStaticRoot", false).f_Boolean();

		CStr CertificateOrganization = fp_GetConfigValue("CertificateOrganization", DProductCompany).f_String();
		CStr CertificateCountry = fp_GetConfigValue("CertificateCountry", DProductCountry).f_String();
		CStr CertificateLocality = fp_GetConfigValue("CertificateLocality", DProductLocality).f_String();
		CStr CertificateOrganizationalUnit = fp_GetConfigValue("CertificateOrganizationalUnit", DProductOrganizationalUnit).f_String();

		TCMap<CStr, CAlternateSource> AlternateSources;

		TCMap<CStr, CWebAppManagerOptions::CPackage> FilteredPackages;
		for (auto &Package : mp_Options.m_Packages)
		{
			if (!Package.m_bUploadS3)
				FilteredPackages[mp_Options.m_Packages.fs_GetKey(Package)] = Package;
		}

		for (auto &Package : FilteredPackages)
		{
			CStr AlternateSourcesContents;
			TCVector<CStr> DefaultLocations;
			CStr DefaultSourceSubFiltersContents;

			for (auto &AlternateSource : Package.m_AlternateSources)
			{
				CStr SubFilters;
				if (!AlternateSource.m_SearchReplace.f_IsEmpty())
				{
					for (auto &SearchReplace : AlternateSource.m_SearchReplace)
					{
						SubFilters += fg_Format
							(
								"			sub_filter '{}' '{}';\n"
								, fp_DoCustomStringReplacements(SearchReplace.m_Search.f_Replace("{DomainName}", mp_Domain))
								, fp_DoCustomStringReplacements(SearchReplace.m_Replace.f_Replace("{DomainName}", mp_Domain))
							)
						;
					}
				}

				CStr Destination = AlternateSource.m_Destination;
				if (Destination != "Default")
				{
					CStr AlternateSourceConfigName = "AlternateSource_{}"_f << Destination;
					Destination = fp_GetConfigValue(AlternateSourceConfigName, "").f_String();
					if (Destination.f_IsEmpty())
					{
						DMibLogWithCategory(WebAppManager, Error, "Missing alternate source in config file: {}", AlternateSourceConfigName);
						co_return {};
					}
				}
				else
				{
					if (!AlternateSource.m_SearchReplace.f_IsEmpty())
					{
						if (DefaultSourceSubFiltersContents.f_IsEmpty() && !SubFilters.f_IsEmpty())
							DefaultSourceSubFiltersContents += gc_SubFilterConfig;

						DefaultSourceSubFiltersContents += SubFilters;
					}
					else
						DefaultLocations.f_Insert(AlternateSource.m_Pattern);

					continue;
				}

				AlternateSourcesContents += fg_Format
					(
						"		location ~ {}\n"
						"		{{\n"
						"{{SecurityHeaders}\n"
						"			resolver 8.8.8.8;\n"
						"			proxy_ssl_server_name on;\n"
						"			proxy_pass https://{}$1$is_args$args;\n"
						"			proxy_buffer_size 128k;\n"
						"			proxy_buffers 4 256k;\n"
						"			proxy_busy_buffers_size 256k;\n"
						"{}{}"
						"		}\n"
						, AlternateSource.m_Pattern
						, Destination
						, gc_SubFilterConfig
						, SubFilters
					)
				;
			}

			auto &AlternateSource = AlternateSources[Package.f_GetName()];
			AlternateSource.m_Locations = fg_Move(AlternateSourcesContents);
			if (!DefaultLocations.f_IsEmpty())
				AlternateSource.m_DefaultLocation = "{}"_f << CStr::fs_Join(DefaultLocations, "|");

			AlternateSource.m_DefaultSourceSubFilters = fg_Move(DefaultSourceSubFiltersContents);
		}

		CResults SetupResults;
		{
			auto BlockingActorCheckout = fg_BlockingActor();
			SetupResults = co_await
				(
					g_Dispatch(BlockingActorCheckout) /
					[
						ProgramDirectory = CFile::fs_GetProgramDirectory()
						, NginxDirectory
						, bIsStaging = mp_bIsStaging
						, Domain = mp_Domain
						, DomainCookie = mp_DomainCookie
						, Packages = FilteredPackages
						, DhParamFile
						, ConfigFile
						, CertificateOrganization
						, CertificateCountry
						, CertificateLocality
						, CertificateOrganizationalUnit
						, FastCGIFile
						, ManagerName = mp_Options.m_ManagerName
					]
					() mutable -> CResults
					{
						CResults Results;

						CFile::fs_CreateDirectory(NginxDirectory + "/root");
						CFile::fs_CreateDirectory(NginxDirectory + "/logs");
						CFile::fs_CreateDirectory(NginxDirectory + "/.tmp");
						CFile::fs_CreateDirectory(NginxDirectory + "/certificates");

						Results.m_CertificateFile = NginxDirectory + "/certificates/web.pem";
						Results.m_CertificateKeyFile = NginxDirectory + "/certificates/web.key";
						CStr CertificateRequestFile = NginxDirectory + "/certificates/web.csr";

						CStr CaCertificateFile = NginxDirectory + "/certificates/web_ca.pem";
						CStr CaCertificateKeyFile = NginxDirectory + "/certificates/web_ca.key";

						if (bIsStaging)
						{
							Results.m_CertificateFile = NginxDirectory + "/certificates/web-staging.pem";
							Results.m_CertificateKeyFile = NginxDirectory + "/certificates/web-staging.key";
							CertificateRequestFile = NginxDirectory + "/certificates/web-staging.csr";
						}

						if (!CFile::fs_FileExists(Results.m_CertificateFile))
						{
		#ifdef DMibDebug
							CPublicKeySetting KeySettings = CPublicKeySettings_EC_secp384r1{};
		#else
							CPublicKeySetting KeySettings = CPublicKeySettings_RSA{8192};
		#endif

							TCMap<CStr, CStr> RelativeDistinguishedNames;

							RelativeDistinguishedNames["C"] = CertificateCountry;
							RelativeDistinguishedNames["L"] = CertificateLocality;
							RelativeDistinguishedNames["O"] = CertificateOrganization;
							RelativeDistinguishedNames["OU"] = CertificateOrganizationalUnit;

							CByteVector CertData;
							CByteVector CertRequestData;
							CSecureByteVector KeyData;
							CByteVector CaCertData;
							CSecureByteVector CaKeyData;

							TCVector<CStr> Subjects = fg_CreateVector<CStr>(Domain, "*." + Domain);

							if (CFile::fs_FileExists(CaCertificateFile) && CFile::fs_FileExists(CaCertificateKeyFile))
							{
								CaCertData = CFile::fs_ReadFile(CaCertificateFile);
								CaKeyData = CFile::fs_ReadFileSecure(CaCertificateKeyFile);
							}
							else
							{
								CCertificateOptions Options;
								Options.m_CommonName = fg_Format("{} CA {nfh,sj16,sf0}", ManagerName, fg_GetHighEntropyRandomInteger<uint64>());
								Options.m_RelativeDistinguishedNames = RelativeDistinguishedNames;
								Options.m_KeySetting = KeySettings;
								Options.f_MakeCA();

								CCertificateSignOptions SignOptions;
								SignOptions.m_Days = 365*20;
								SignOptions.f_AddExtension_SubjectKeyIdentifier();

								CCertificate::fs_GenerateSelfSignedCertAndKey
									(
										Options
										, CaCertData
										, CaKeyData
										, SignOptions
									)
								;
								CFile::fs_WriteFile(CaCertData, CaCertificateFile);
		#ifdef DPlatformFamily_Windows
								CFile::fs_WriteFile(CaCertData, NginxDirectory + "/certificates/web_ca.crt");
		#endif
								CFile::fs_WriteFileSecure(CaKeyData, CaCertificateKeyFile);
							}

							CCertificateOptions Options;
							Options.m_CommonName = Domain;
							Options.m_RelativeDistinguishedNames = RelativeDistinguishedNames;
							Options.m_Hostnames = Subjects;
							Options.m_KeySetting = KeySettings;

							CCertificateSignOptions SignOptions;
							SignOptions.m_Serial = 2;
							SignOptions.m_Days = 824;
							SignOptions.f_AddExtension_AuthorityKeyIdentifier();
							Options.f_AddExtension_BasicConstraints(false);
							Options.f_AddExtension_KeyUsage(EKeyUsage_KeyEncipherment | EKeyUsage_DigitalSignature);

							CCertificate::fs_GenerateClientCertificateRequest(Options, CertRequestData, KeyData);
							CCertificate::fs_SignClientCertificate(CaCertData, CaKeyData, CertRequestData, CertData, SignOptions);

							CFile::fs_WriteFile(CertData, Results.m_CertificateFile);
							CFile::fs_WriteFileSecure(KeyData, Results.m_CertificateKeyFile);
							CFile::fs_WriteFile(CertRequestData, CertificateRequestFile);
						}

						if (CFile::fs_FileExists(DhParamFile))
							Results.m_bHasDHParamFile = true;

						CFile::fs_SetUnixAttributesRecursive(NginxDirectory + "/certificates", EFileAttrib_UserRead, EFileAttrib_UserRead | EFileAttrib_UserWrite | EFileAttrib_UserExecute);

						Results.m_ConfigContents = CFile::fs_ReadStringFromFile(ProgramDirectory + "/Source/Malterlib_WebApp_App_WebAppManager_Nginx.conf");
						CFile::fs_DiffCopyFileOrDirectory(ProgramDirectory + "/Source/Malterlib_WebApp_App_WebAppManager_FastCGI.conf", FastCGIFile, nullptr);

						{
							for (auto &Package : Packages)
							{
								if (!Package.f_IsServer())
									continue;

								CStr RedirectContents;

								if (!Package.m_RedirectsFile.f_IsEmpty())
								{
									CStr RedirectPath = fg_Format("{}/{}/{}", CFile::fs_GetProgramDirectory(), Package.f_GetName(), Package.m_RedirectsFile);

									try
									{
										CJsonSorted const RedirectJson = CJsonSorted::fs_FromString(CFile::fs_ReadStringFromFile(RedirectPath), RedirectPath);

										for (auto const &Redirect : RedirectJson["redirects"].f_Array())
										{
											CStr Path = Redirect["path"].f_String();
											CStr RedirectTo = Redirect["redirectTo"].f_String();
											CStr Campaign = Redirect["campaign"].f_String();
											CStr ReferrerCookie = Redirect["referrerCookie"].f_String();
											CStr CampaignPercentEncoded;
											CURL::fs_PercentEncode(CampaignPercentEncoded, Campaign);
											RedirectContents += fg_Format
												(
													"			if ($uri ~* ^/{}$) {{\n"
													"{{SecurityHeaders}\n"
													"				add_header Set-Cookie \"{}=$http_referer; Secure; HttpOnly; Path=/; Domain={}\";\n"
													"				return 302 {}?campaign={};\n"
													"			}\n"
													, Path
													, ReferrerCookie
													, DomainCookie
													, RedirectTo
													, CampaignPercentEncoded
												)
											;
										}
									}
									catch (CException const &_Exception)
									{
										DMibError(fg_Format("Failed to generate redirects: {}", _Exception));
									}
								}

								for (auto &Redirect : Package.m_RedirectsTemporary)
								{
									CStr RedirectTo = Redirect.m_To;
									RedirectTo = RedirectTo.f_Replace("{}", "$uri");

									RedirectContents += fg_Format
										(
											"			if ($uri ~* ^{}) {{\n"
											"				return 302 {}$is_args$args;\n"
											"			}\n"
											, Redirect.m_From
											, RedirectTo
										)
									;
								}

								for (auto &Redirect : Package.m_RedirectsPermanent)
								{
									CStr RedirectTo = Redirect.m_To;
									RedirectTo = RedirectTo.f_Replace("{}", "$uri");

									RedirectContents += fg_Format
										(
											"			if ($uri ~* ^{}) {{\n"
											"				return 301 {}$is_args$args;\n"
											"			}\n"
											, Redirect.m_From
											, RedirectTo
										)
									;
								}

								if (!RedirectContents.f_IsEmpty())
									Results.m_Redirects[Package.f_GetName()] = fg_Move(RedirectContents);
							}
						}

						return Results;
					}
				)
			;
		}

		CStr ProgramDirectory = CFile::fs_GetProgramDirectory();

		CStr ConfigContents = SetupResults.m_ConfigContents;

		CStr PidFile = NginxDirectory + "/nginx.pid";

		TCSet<CStr> VariablesToRemove;
		VariablesToRemove["{HTTPDefaultServerLocations}"];
		VariablesToRemove["{HTTPDefaultServerLocations_www}"];
		VariablesToRemove["{HTTPSDefaultServerLocations_www}"];

		TCMap<CStr, CStr> PackageIPs;

		CStr UpstreamServers;
		TCMap<CStr, CStr> Upstreams;
		{
			for (auto &Package : FilteredPackages)
			{
				if (!Package.f_IsServer() || Package.f_IsStatic())
					continue;

				bool bIsFastCGI = Package.m_Type == CWebAppManagerOptions::EPackageType_FastCGI;

				auto &UpstreamName = Upstreams[Package.f_GetName()];

				UpstreamName = "upstream_{}"_f << Package.f_GetName();

				UpstreamServers += "\n	# {} Upstream\n\n"_f << Package.f_GetName();
				UpstreamServers += "	upstream {}\n"_f << UpstreamName;
				UpstreamServers += "	{\n";

				if (Package.m_StickyHeader.f_IsEmpty() && Package.m_StickyCookie.f_IsEmpty())
					UpstreamServers += "		ip_hash; # for sticky sessions\n";

				mint nUpstream = 0;
				for (auto &AppLaunch : mp_AppLaunches)
				{
					if (AppLaunch.f_GetKey().m_PackageName != Package.f_GetName())
						continue;
					++nUpstream;
					CStr IPAddress = fp_GetAppIPAddress(AppLaunch, false);
					for (mint iPort = mp_LocalPort; iPort < mp_LocalPort + Package.m_PortConcurrency; ++iPort)
						UpstreamServers += "\t\tserver {}{}{} max_fails=30 fail_timeout=30s;\n"_f << IPAddress << AppLaunch.f_PortDelim() << iPort;
					PackageIPs[Package.f_GetName()] = IPAddress;
				}

				if (bIsFastCGI)
					UpstreamServers += "\t\tkeepalive {};\n"_f << nUpstream;

				UpstreamServers += "	}\n\n";

				if (!Package.m_StickyHeader.f_IsEmpty())
				{
					CStr PreviousUpstream = UpstreamName;
					UpstreamName = "$upstreamStickyHeader_{}"_f << Package.f_GetName();

					UpstreamServers += "	map $http_{} {}\n"_f << Package.m_StickyHeader << UpstreamName;
					UpstreamServers += "	{\n";
					UpstreamServers += "		default {};\n"_f << PreviousUpstream;

					for (auto &AppLaunch : mp_AppLaunches)
					{
						if (AppLaunch.f_GetKey().m_PackageName != Package.f_GetName())
							continue;
						for (mint iPort = mp_LocalPort; iPort < mp_LocalPort + Package.m_PortConcurrency; ++iPort)
							UpstreamServers += "		{} {}{}{};\n"_f << AppLaunch.m_BackendIdentifier << fp_GetAppIPAddress(AppLaunch, false) << AppLaunch.f_PortDelim() << iPort;
					}

					UpstreamServers += "	}\n\n";
				}
				if (!Package.m_StickyCookie.f_IsEmpty())
				{
					CStr PreviousUpstream = UpstreamName;
					UpstreamName = "$upstreamStickyCookie_{}"_f << Package.f_GetName();

					UpstreamServers += "	map $cookie_{} {}\n"_f << Package.m_StickyCookie << UpstreamName;
					UpstreamServers += "	{\n";
					UpstreamServers += "		default {};\n"_f << PreviousUpstream;

					for (auto &AppLaunch : mp_AppLaunches)
					{
						if (AppLaunch.f_GetKey().m_PackageName != Package.f_GetName())
							continue;
						for (mint iPort = mp_LocalPort; iPort < mp_LocalPort + Package.m_PortConcurrency; ++iPort)
							UpstreamServers += "		{} {}{}{};\n"_f << AppLaunch.m_BackendIdentifier << fp_GetAppIPAddress(AppLaunch, false) << AppLaunch.f_PortDelim() << iPort;
					}

					UpstreamServers += "	}\n\n";
				}
			}
			UpstreamServers += "{WebAppManagerUpstream}\n";
			VariablesToRemove["{WebAppManagerUpstream}"];
		}

		ConfigContents = ConfigContents.f_Replace("{WebAppManagerUpstream}", UpstreamServers);

		VariablesToRemove["{WebAppManagerHTTPServers}"];
		if (mp_Options.m_bRedirectWWW)
		{
					CStr Section = R"---(
	server
	{
		listen {BindTo}{Port};
		server_name www.{DomainName};

{HTTPDefaultServerLocations_www}

		location /
		{
			add_header Set-Cookie "{HTTPRedirectReferrerCookie}=$http_referer; Secure; HttpOnly; Path=/; Domain={DomainNameCookie}";
			return 302 https://$host{SSLPortRewrite}$request_uri;
		}
	}
{WebAppManagerHTTPServers}
)---";

			ConfigContents = ConfigContents.f_Replace("{WebAppManagerHTTPServers}", Section);
		}

		CStr Servers;
		TCMap<CStr, CStr> VariablesToReplace;
		bool bHasDefaultServer = false;
		{
			TCMap<CStr, CStr> SubPathServers;
			for (int i = 0; i < 2; ++i)
			{
				bool bIsSubPackage = i == 0;

				for (auto &Package : FilteredPackages)
				{
					if (!Package.f_IsServer())
						continue;

					if (bIsSubPackage && Package.m_SubPath.f_IsEmpty())
						continue;
					else if (!bIsSubPackage && !Package.m_SubPath.f_IsEmpty())
						continue;

					bool bIsFastCGI = Package.m_Type == CWebAppManagerOptions::EPackageType_FastCGI;
					bool bIsWebsocket = Package.m_Type == CWebAppManagerOptions::EPackageType_Websocket;
					bool bIsMeteor = Package.m_Type == CWebAppManagerOptions::EPackageType_Meteor;
					bool bIsStatic = Package.f_IsStatic();

					auto &UpstreamName = Upstreams[Package.f_GetName()];

					CStr Server;
					if (bIsStatic)
						Server = g_pStaticServerTemplate[i];
					else if (bIsFastCGI)
						Server = g_pFastCGIServerTemplate[i];
					else if (bIsWebsocket)
						Server = g_pWebsocketServerTemplate[i];
					else
						Server = g_pServerTemplate[i];

					if (!bIsSubPackage && bEnableSeparateStaticRoot && Package.m_Type == CWebAppManagerOptions::EPackageType_Meteor)
					{
						Server += g_pServerSeparateStaticRootTemplate;
						Server = Server.f_Replace("{ServerNameStatic}", fp_GetPackageHostname(Package.f_GetName(), EHostnamePrefix_Static));
						Server = Server.f_Replace("{ServerNameStaticSource}", fp_GetPackageHostname(Package.f_GetName(), EHostnamePrefix_StaticSource));
					}

					CStr ServerName = fp_GetPackageHostname(Package.f_GetName(), EHostnamePrefix_None);

					bool bIsMainServer = ServerName == mp_Domain && Package.m_SubPath.f_IsEmpty();

					Server = Server.f_Replace("{AllowRobots}", fp_GetAllowRobots((Package.m_bAllowRobots || mp_Options.m_bAllowRobots) && mp_bAllowRobots).f_Replace("\n", "\\n"));

					if (Package.m_bDefaultServer && !mp_bCheckForInvalidHost)
					{
						bHasDefaultServer = true;
						Server = Server.f_Replace("{DefaultServer}", "default_server");
					}
					else
						Server = Server.f_Replace("{DefaultServer}", "");

					if (bIsMainServer && mp_bCheckForInvalidHost)
						Server = Server.f_Replace("{CheckServerNameLogic}", g_pCheckServerName);
					else
						Server = Server.f_Replace("{CheckServerNameLogic}", "");

					if (bIsMainServer && mp_Options.m_bServeAllSubdomains)
						Server = Server.f_Replace("{ServerName}", ("{} ~^[a-z0-9-]*\\.{}$"_f << ServerName << ServerName.f_Replace(".", "\\.")).f_GetStr());
					else
						Server = Server.f_Replace("{ServerName}", ServerName);

					Server = Server.f_Replace("{SubPath}", Package.m_SubPath);

					if (bIsFastCGI)
						Server = Server.f_Replace("{FastCGIFile}", FastCGIFile);

					Server = Server.f_Replace("{PackageName}", Package.f_GetName());
					Server = Server.f_Replace("{UpstreamSticky}", UpstreamName);
					Server = Server.f_Replace("{Upstream}", fg_Format("upstream_{}", Package.f_GetName()));

					if (bIsStatic)
						Server = Server.f_Replace("{StaticRoot}", fp_GetPackageRoot(Package.f_GetName()).f_EscapeStrNoQuotes());
					else if (bIsMeteor)
						Server = Server.f_Replace("{StaticRoot}", fg_Format("{}/{}/programs/web.browser", ProgramDirectory, Package.f_GetName()).f_EscapeStrNoQuotes());
					else
					{
						CStr StaticSourcePath = Package.m_StaticSourcePath;
						if (!StaticSourcePath)
							StaticSourcePath = "static";
						Server = Server.f_Replace("{StaticRoot}", fg_Format("{}/{}/{}", ProgramDirectory, Package.f_GetName(), StaticSourcePath).f_EscapeStrNoQuotes());
					}

					auto StaticUriRegex = Package.m_StaticUriRegex;
					if (!StaticUriRegex)
						StaticUriRegex = "^/[A-Za-z0-9]{40}\\.(css|js)$";

					Server = Server.f_Replace("{StaticUriRegex}", StaticUriRegex.f_EscapeStrNoQuotes());

					VariablesToReplace[fg_Format("{{ServerName_{}}", Package.f_GetName())] = ServerName;
					VariablesToReplace[fg_Format("{{ServerNameEscaped_{}}", Package.f_GetName())] = ServerName.f_Replace(".", "\\.");

					VariablesToRemove[("{{ServerNameExtra_{}}"_f << Package.f_GetName()).f_GetStr()];
					VariablesToRemove[("{{ServerAccessCheck_{}}"_f << Package.f_GetName()).f_GetStr()];
					VariablesToRemove[("{{ServerRedirect_{}}"_f << Package.f_GetName()).f_GetStr()];
					VariablesToRemove[("{{CustomizationInServer_{}}"_f << Package.f_GetName()).f_GetStr()];
					VariablesToRemove[("{{ServerRootOptions_{}}"_f << Package.f_GetName()).f_GetStr()];

					CStr StaticPackages;

					if (bIsMainServer)
					{
						for (auto &Package : FilteredPackages)
						{
							if (Package.f_IsDynamicServer() || Package.m_StaticPath.f_IsEmpty())
								continue;

							StaticPackages += "		location {}\n"_f << Package.m_StaticPath;
							StaticPackages += "		{\n";
							StaticPackages += "			alias {}/{};\n"_f << ProgramDirectory << (Package.f_GetName() / Package.m_StaticSourcePath);
							if (Package.f_IsStatic())
								StaticPackages += "			gzip_static {GZipStatic};\n";
							StaticPackages += "{SecurityHeaders}\n";
							StaticPackages += "			add_header Cache-Control no-cache;\n";
							StaticPackages += "			access_log logs/static_access_{}.log;\n"_f << Package.f_GetName();
							StaticPackages += "		}\n";
						}
					}

					Server = Server.f_Replace("{StaticPackages}", StaticPackages);
					if (!bIsSubPackage)
						Server = Server.f_Replace("{SubPackages}", SubPathServers[ServerName]);
					Server = Server.f_Replace("{PathRedirect}",  SetupResults.m_Redirects[Package.f_GetName()]);
					auto &AlternateSource = AlternateSources[Package.f_GetName()];
					Server = Server.f_Replace("{AlternateSources}", AlternateSource.m_Locations);
					Server = Server.f_Replace("{DefaultSourceSubFilters}", AlternateSource.m_DefaultSourceSubFilters);
					Server = Server.f_Replace("{GZipStatic}", AlternateSource.m_DefaultSourceSubFilters.f_IsEmpty() ? gc_Str<"always">.m_Str : gc_Str<"off">.m_Str);
					Server = Server.f_Replace("{DefaultLocation}", AlternateSource.m_DefaultLocation ? AlternateSource.m_DefaultLocation : CStr("/.*"));

					if (bIsMainServer && mp_Options.m_bRedirectWWW)
					{
						Server += R"---(
	server
	{
		listen {BindTo}{SSLPort} {DefaultServer};
		{DisableIPV6} listen [::]:{SSLPort} {DefaultServer};
		http2  on;
		server_name www.{DomainName};

{HTTPSDefaultServerLocations_www}

		location /
		{
{SecurityHeaders}
			add_header Set-Cookie "{HTTPRedirectReferrerCookie}=$http_referer; Secure; HttpOnly; Path=/; Domain={DomainNameCookie}";
			return 302 https://{DomainName}{SSLPortRewrite}$request_uri;
		}
		return 302 https://{DomainName}{SSLPortRewrite}$request_uri;
	}
)---";
					}

					if (bIsMainServer)
					{
						Server += "\n";
						Servers = Server + Servers;
					}
					else if (bIsSubPackage)
					{
						auto &ParentServer = SubPathServers[ServerName];
						ParentServer += "\n";
						ParentServer += Server;
					}
					else
					{
						Servers += "\n";
						Servers += Server;
					}
				}
			}
			Servers += "{WebAppManagerServers}\n";
			VariablesToRemove["{WebAppManagerServers}"];
		}
		ConfigContents = ConfigContents.f_Replace("{WebAppManagerServers}", Servers);

		if (mp_pCustomization)
		{
			mp_pCustomization->f_ManipulateNginxConfig
				(
					ConfigContents
					, FastCGIFile
					, PackageIPs
					, fp_GetImpl()
				)
			;
		}

		for (auto &ToRemove : VariablesToRemove)
			ConfigContents = ConfigContents.f_Replace(ToRemove, "");

		{
			CStr RedirectReferrerCookie = mp_Options.m_HTTPRedirectReferrerCookie.f_IsEmpty() ? CStr("RedirectReferrer") : mp_Options.m_HTTPRedirectReferrerCookie;
			ConfigContents = ConfigContents.f_Replace("{HTTPRedirectReferrerCookie}", RedirectReferrerCookie);
		}

		for (auto &ReplaceWith : VariablesToReplace)
			ConfigContents = ConfigContents.f_Replace(VariablesToReplace.fs_GetKey(ReplaceWith), ReplaceWith);

		if (mp_bCheckForInvalidHost)
			ConfigContents = ConfigContents.f_Replace("{CheckServerNameLogicPort80}", g_pCheckServerName);
		else
			ConfigContents = ConfigContents.f_Replace("{CheckServerNameLogicPort80}", "");

		ConfigContents = ConfigContents.f_Replace("{DomainName}", mp_Domain);
		ConfigContents = ConfigContents.f_Replace("{DomainNameCookie}", mp_DomainCookie);
		ConfigContents = ConfigContents.f_Replace("{DomainNameEscaped}", mp_Domain.f_Replace(".", "\\."));

		ConfigContents = ConfigContents.f_Replace("{Root}", (NginxDirectory + "/root").f_EscapeStr());
		ConfigContents = ConfigContents.f_Replace("{Port}", CStr::fs_ToStr(mp_WebPort));
		ConfigContents = ConfigContents.f_Replace("{SSLPort}", CStr::fs_ToStr(mp_WebSSLPort));
		if (mp_WebSSLPort == 443)
			ConfigContents = ConfigContents.f_Replace("{SSLPortRewrite}", "");
		else
			ConfigContents = ConfigContents.f_Replace("{SSLPortRewrite}", ":" + CStr::fs_ToStr(mp_WebSSLPort));
		ConfigContents = ConfigContents.f_Replace("{Certificate}", SetupResults.m_CertificateFile.f_EscapeStr());
		ConfigContents = ConfigContents.f_Replace("{CertificateKey}", SetupResults.m_CertificateKeyFile.f_EscapeStr());
		ConfigContents = ConfigContents.f_Replace("{PidFile}", PidFile.f_EscapeStr());
		ConfigContents = ConfigContents.f_Replace("{WorkerMaxOpenedFiles}", CStr::fs_ToStr(fs_GetNginxWorkerFileLimits()));

#ifdef DPlatformFamily_Windows
		ConfigContents = ConfigContents.f_Replace("{NginxUserLine}", "");
#else
		if (mp_bRunningElevated)
			ConfigContents = ConfigContents.f_Replace("{NginxUserLine}", ("user {} {};"_f << mp_NginxUser.m_UserName << mp_NginxUser.m_GroupName).f_GetStr());
		else
			ConfigContents = ConfigContents.f_Replace("{NginxUserLine}", "");
#endif

		{
			if (SetupResults.m_bHasDHParamFile)
				ConfigContents = ConfigContents.f_Replace("{ssl_dhparam}", fg_Format("ssl_dhparam {};", DhParamFile));
			else
				ConfigContents = ConfigContents.f_Replace("{ssl_dhparam}", "");
		}

		ConfigContents = ConfigContents.f_Replace("{AllowRobots}", fp_GetAllowRobots(mp_Options.m_bAllowRobots && mp_bAllowRobots).f_Replace("\n", "\\n"));

		{
			CStr ContentTypesContents;
			for (auto &Extensions : CWebAppManagerActor::fsp_GetContentTypes())
			{
				if (Extensions.f_IsEmpty())
					continue;

				auto &ContentType = TCMap<CStr, TCVector<CStr>>::fs_GetKey(Extensions);

				ContentTypesContents += "		{}	"_f << ContentType;

				for (auto &Extension : Extensions)
					ContentTypesContents += " {}"_f << Extension;

				ContentTypesContents += ";\n";

			}
			ConfigContents = ConfigContents.f_Replace("{ContentTypes}", ContentTypesContents);
		}
		CStr CspSelfHost = mp_Domain;
		if (mp_WebSSLPort != 443)
			CspSelfHost = "{}:{}"_f << mp_Domain << mp_WebSSLPort;

		{
			CStr SecurityHeaders;
			CStr ContentSecurityPolicy = "default-src 'none' {};"_f << mp_Options.m_ContentSecurity_DefaultSrc;
			ContentSecurityPolicy += " prefetch-src 'self' https://*.{0} https://{0} {1} ;"_f << CspSelfHost << mp_Options.m_ContentSecurity_PrefetchSrc;
			ContentSecurityPolicy += " img-src 'self' data: https://*.{0} https://{0} {1} ;"_f << CspSelfHost << mp_Options.m_ContentSecurity_ImgSrc;
			ContentSecurityPolicy += " font-src 'self' data: https://*.{0} https://{0} {1} ;"_f << CspSelfHost << mp_Options.m_ContentSecurity_FontSrc;
			ContentSecurityPolicy += " media-src 'self' https://*.{0} https://{0} {1} ;"_f << CspSelfHost << mp_Options.m_ContentSecurity_MediaSrc;
			ContentSecurityPolicy += " script-src 'self' https://*.{0} https://{0} {1};"_f << CspSelfHost << mp_Options.m_ContentSecurity_ScriptSrc;
			ContentSecurityPolicy += " style-src 'self' https://*.{0} https://{0} {1} ;"_f << CspSelfHost << mp_Options.m_ContentSecurity_StyleSrc;
			ContentSecurityPolicy += " frame-src 'self' https://*.{0} https://{0} {1} ;"_f << CspSelfHost << mp_Options.m_ContentSecurity_FrameSrc;
			ContentSecurityPolicy += " connect-src 'self' https://*.{0} https://{0} wss://*.{0} wss://{0} {1} ;"_f << CspSelfHost << mp_Options.m_ContentSecurity_ConnectSrc;
			ContentSecurityPolicy += " child-src 'self' https://*.{0} https://{0} {1} ;"_f << CspSelfHost << mp_Options.m_ContentSecurity_ChildSrc;
			ContentSecurityPolicy += " manifest-src 'self' https://*.{0} https://{0} {1} ;"_f << CspSelfHost << mp_Options.m_ContentSecurity_ManifestSrc;
			ContentSecurityPolicy += " form-action 'self' https://*.{0} https://{0} {1} ;"_f << CspSelfHost << mp_Options.m_ContentSecurity_FormAction;
			ContentSecurityPolicy += " object-src 'none' {} ;"_f << mp_Options.m_ContentSecurity_ObjectSrc;

			if (mp_Options.m_ContentSecurity_ReportURI)
			{
				CStr ContentSecurityReportURI = mp_Options.m_ContentSecurity_ReportURI;
				ContentSecurityPolicy += " report-uri {} ;"_f << ContentSecurityReportURI;
			}

			if (mp_Options.m_ContentSecurity_FrameAncestors)
			{
				CStr ContentSecurityFrameAncestors = mp_Options.m_ContentSecurity_FrameAncestors;
				ContentSecurityPolicy += " frame-ancestors {} ;"_f << ContentSecurityFrameAncestors;
			}

			ContentSecurityPolicy = ContentSecurityPolicy.f_Replace("{DomainName}", mp_Domain);
			ContentSecurityPolicy = ContentSecurityPolicy.f_Replace("{SSLPortRewrite}", "");
			ContentSecurityPolicy = fp_DoCustomStringReplacements(ContentSecurityPolicy);

			SecurityHeaders += "			add_header Strict-Transport-Security \"max-age=63072000; includeSubdomains; preload\" always;\n";
			SecurityHeaders += "			add_header Content-Security-Policy \"{}\" always;\n"_f << ContentSecurityPolicy;
			SecurityHeaders += "			add_header X-Content-Type-Options \"nosniff\" always;\n";
			SecurityHeaders += "			add_header X-XSS-Protection \"0\" always;\n";
			SecurityHeaders += "			add_header Referrer-Policy \"same-origin\" always;\n";

			if (!mp_Options.m_AccessControl_AllowMethods.f_IsEmpty())
				SecurityHeaders += "			add_header Access-Control-Allow-Methods \"{}\" always;\n"_f << mp_Options.m_AccessControl_AllowMethods;
			if (!mp_Options.m_AccessControl_AllowHeaders.f_IsEmpty())
				SecurityHeaders += "			add_header Access-Control-Allow-Headers \"{}\" always;\n"_f << mp_Options.m_AccessControl_AllowHeaders;
			if (!mp_Options.m_AccessControl_AllowOrigin.f_IsEmpty())
			{
				SecurityHeaders += "			add_header Access-Control-Allow-Origin \"{}\" always;\n"_f << mp_Options.m_AccessControl_AllowOrigin;
				SecurityHeaders += "			add_header Vary \"Origin, Accept-Encoding\" always;\n";
			}
			if (!mp_Options.m_AccessControl_MaxAge.f_IsEmpty())
				SecurityHeaders += "			add_header Access-Control-Max-Age \"{}\" always;\n"_f << mp_Options.m_AccessControl_MaxAge;

			ConfigContents = ConfigContents.f_Replace("{SecurityHeadersNoFrameOptions}", SecurityHeaders);

			if (!mp_Options.m_ContentSecurity_FrameAncestors.f_IsEmpty())
				SecurityHeaders += "			add_header X-Frame-Options \"DENY\" always;\n";

			ConfigContents = ConfigContents.f_Replace("{SecurityHeaders}", SecurityHeaders);
		}

		if (mp_BindTo)
			ConfigContents = ConfigContents.f_Replace("{BindTo}", mp_BindTo + ":");
		else
			ConfigContents = ConfigContents.f_Replace("{BindTo}", "");

		if (mp_bEnableIPV6)
			ConfigContents = ConfigContents.f_Replace("{DisableIPV6}", "");
		else
			ConfigContents = ConfigContents.f_Replace("{DisableIPV6}", "#");

		if (mp_bCheckForInvalidHost)
			ConfigContents = ConfigContents.f_Replace("{DisableSSLRejectHandshake}", "");
		else
			ConfigContents = ConfigContents.f_Replace("{DisableSSLRejectHandshake}", "#");

		if (bHasDefaultServer)
		{
			ConfigContents = ConfigContents.f_Replace("{DefaultServer}", "default_server");
			ConfigContents = ConfigContents.f_Replace("{DefaultServerWhenNoDefault}", "");
		}
		else
		{
			ConfigContents = ConfigContents.f_Replace("{DefaultServer}", "");
			ConfigContents = ConfigContents.f_Replace("{DefaultServerWhenNoDefault}", "default_server");
		}

		{
			auto BlockingActorCheckout = fg_BlockingActor();
			co_await
				(
					g_Dispatch(BlockingActorCheckout)
					/ [ConfigFile, ConfigContents, UserName = mp_NginxUser.m_UserName, GroupName = mp_NginxUser.m_GroupName, NginxDirectory, bRunningElevated = mp_bRunningElevated]()
					{
						CFile::fs_WriteStringToFile(ConfigFile, ConfigContents, false);

						CFile::fs_SetOwnerAndGroupRecursive(NginxDirectory, UserName, GroupName);
						if (bRunningElevated)
							CFile::fs_SetOwnerAndGroupRecursive(NginxDirectory + "/certificates", "root", GroupName);
					}
				)
			;
		}

		mp_CertificateDeployActor = fg_Construct(mp_AppState.m_DistributionManager, mp_AppState.m_TrustManager);

		{
			auto fGetFilesSettings = [&](CStr const &_Path)
				{
					CWebCertificateDeployActor::CCertificateFileSettings FileSettings;
					FileSettings.m_Path = _Path;
					FileSettings.m_Attributes = EFileAttrib_UserRead;
					FileSettings.m_User = "root";
					FileSettings.m_Group = mp_NginxUser.m_GroupName;

					return FileSettings;
				}
			;

			CWebCertificateDeployActor::CDomainSettings DomainSettings;
			DomainSettings.m_DomainName = mp_Domain;
			DomainSettings.m_FileSettings_Ec = CWebCertificateDeployActor::CCertificateFilesSettings
				{
					fGetFilesSettings(SetupResults.m_CertificateKeyFile)
					, fGetFilesSettings(SetupResults.m_CertificateFile)
				}
			;

			DomainSettings.m_fOnStatusChange = g_ActorFunctor / [](CHostInfo _HostInfo, CWebCertificateDeployActor::CDomainStatus _Status) -> TCFuture<void>
				{
					if (_Status.m_Severity == CWebCertificateDeployActor::EStatusSeverity_Error)
						DMibLogWithCategory(Certificate, Error, "{}", _Status.m_Description);
					else if (_Status.m_Severity == CWebCertificateDeployActor::EStatusSeverity_Warning)
						DMibLogWithCategory(Certificate, Warning, "{}", _Status.m_Description);
					else
						DMibLogWithCategory(Certificate, Info, "{}", _Status.m_Description);

					co_return {};
				}
			;

			DomainSettings.m_fOnCertificateUpdated = g_ActorFunctor / [this](CStr _DomainName, CWebCertificateDeployActor::ECertificate _Certificate) -> TCFuture<void>
				{
					if (!mp_NginxLaunch)
						co_return {};

#ifndef DPlatformFamily_Windows
					co_await mp_NginxLaunch(&CProcessLaunchActor::f_Signal, 1); // SIGHUP
#endif
					co_return {};
				}
			;

			mp_CertificateDeploySubscription = co_await mp_CertificateDeployActor(&CWebCertificateDeployActor::f_AddDomain, fg_Move(DomainSettings));
		}

		co_await mp_CertificateDeployActor(&CWebCertificateDeployActor::f_Start);

		co_return {};
	}

	TCFuture<void> CWebAppManagerActor::fp_StartNginx()
	{
		if (mp_NginxLaunch || mp_bStopped || f_IsDestroyed() || !mp_bStartNginx)
			co_return {}; // Launch already in progress

		CStr ProgramDirectory = CFile::fs_GetProgramDirectory();
		CStr NginxDirectory = fp_GetDataPath("nginx");
		CStr NginxConfig = NginxDirectory + "/nginx.conf";

		TCVector<CStr> Arguments;
		Arguments.f_Insert("-c");
		Arguments.f_Insert(NginxConfig);

		TCPromise<void> Promise;

		CProcessLaunchActor::CLaunch Launch = CProcessLaunchParams::fs_LaunchExecutable
			(
				ProgramDirectory + "/bin/nginx"
				, Arguments
				, NginxDirectory
				, [this, Promise](CProcessLaunchStateChangeVariant const &_Change, fp64 _TimeSinceStart)
				{
					switch (_Change.f_GetTypeID())
					{
					case EProcessLaunchState_Launched:
						{
							if (mp_bStopped || f_IsDestroyed())
							{
								if (mp_NginxLaunch)
									mp_NginxLaunch(&CProcessLaunchActor::f_StopProcess).f_DiscardResult();
								Promise.f_SetException(DMibErrorInstance("Application is being destroyed"));
							}
							else
								Promise.f_SetResult();
						}
						break;
					case EProcessLaunchState_Exited:
						{
							if (!mp_bStopped && !f_IsDestroyed())
							{
								DLogWithCategory(nginx, Info, "Scheduling relaunch of nginx in 10 seconds");
								fg_Timeout(10.0) > [this]() -> TCFuture<void>
									{
										if (!mp_bStopped && !f_IsDestroyed())
											fp_StartNginx() > fg_LogError("nginx", "Failed to launch nginx");

										co_return {};
									}
								;
							}
							mp_NginxLaunch.f_Clear();
							mp_NginxLaunchSubscription.f_Clear();
						}
						break;
					case EProcessLaunchState_LaunchFailed:
						{
							Promise.f_SetException(DMibErrorInstance(fg_Format("nginx launch failed: {}", _Change.f_Get<EProcessLaunchState_LaunchFailed>())));
							mp_NginxLaunch.f_Clear();
							mp_NginxLaunchSubscription.f_Clear();
						}
						break;
					}
				}
			)
		;

		Launch.m_ToLog = CProcessLaunchActor::ELogFlag_All;
		Launch.m_LogName = "nginx";
		Launch.m_Params.m_bCreateNewProcessGroup = true;

		auto &Params = Launch.m_Params;

		Params.m_bAllowExecutableLocate = true;
		Params.m_bShowLaunched = false;

		{
			auto &Limit = Params.m_Limits[EProcessLimit_OpenedFiles];
			auto MaxFiles = NProcess::NPlatform::fg_Process_GetMaxFilesPerProc();
			if (MaxFiles)
			{
				Limit.m_Value = fg_Min(MaxFiles, fs_GetNginxFileLimits(mp_AppLaunches.f_GetLen()));
				Limit.m_MaxValue = Limit.m_Value;
			}
		}

		fs_SetupEnvironment(Params);
		Params.m_bMergeEnvironment = true;

		Params.m_Environment["HOME"] = NginxDirectory;
		Params.m_Environment["TMPDIR"] = NginxDirectory + "/.tmp";
#ifdef DPlatformFamily_macOS
		Params.m_Environment["MalterlibOverrideHome"] = "true";
#endif
#ifdef DPlatformFamily_Windows
		Params.m_Environment["TMP"] = NginxDirectory + "/.tmp";
		Params.m_Environment["TEMP"] = NginxDirectory + "/.tmp";
#endif

#ifdef DPlatformFamily_Windows
		// This causes access denied when trying to create memory mapping in nginx process
		//Params.m_RunAsUser = mp_NginxUser.m_UserName;
		//Params.m_RunAsUserPassword = fp_GetUserPassword(mp_NginxUser.m_UserName);
		//Params.m_RunAsGroup = mp_NginxUser.m_GroupName;
#endif

		mp_NginxLaunch = fg_ConstructActor<CProcessLaunchActor>();

		auto Subscription = co_await mp_NginxLaunch(&CProcessLaunchActor::f_Launch, fg_Move(Launch), fg_ThisActor(this)).f_Wrap();
		if (!Subscription)
		{
			mp_NginxLaunch.f_Clear();
			co_return Subscription.f_GetException();
		}
		mp_NginxLaunchSubscription = fg_Move(*Subscription);

		co_return co_await Promise.f_MoveFuture();
	}
}
