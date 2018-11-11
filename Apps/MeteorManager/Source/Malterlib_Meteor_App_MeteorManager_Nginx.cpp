
#include "Malterlib_Meteor_App_MeteorManager_Server.h"

#include <Mib/Web/HTTP/URL>
#include <Mib/Network/SSL>
#include <Mib/Concurrency/Actor/Timer>

namespace NMib::NMeteor::NMeteorManager
{
	mint CMeteorManagerActor::fs_GetNginxWorkerFileLimits()
	{
		mint nFilesPerConnection = 2; // nginx incoming + nginx proxy -> node

		return 65536*nFilesPerConnection + 8192;
	}

	mint CMeteorManagerActor::fs_GetNginxFileLimits(mint _nNodes)
	{
		return fs_GetNginxWorkerFileLimits() * _nNodes + 8192;
	}

ch8 const *g_pServerTemplate[2] =
	{
R"---(
		location ~* "^/{SubPath}/[a-z0-9]{40}\.(css|js)$"
		{
			gzip_static always;
			expires max;
			add_header Strict-Transport-Security "max-age=31536000;" always;
			add_header Cache-Control public;
			root {StaticRoot};
			access_log logs/static_access_{PackageName}.log;
		}

		# pass all requests to Meteor
		location /{SubPath}/
		{
{PathRedirect}
{ServerRootOptions_{PackageName}}
			error_page 502 = @{PackageName}_Fallback;
			proxy_pass http://{UpstreamSticky};
			proxy_http_version 1.1;
			proxy_set_header Host $host;
			proxy_set_header Upgrade $http_upgrade; # allow websockets
			proxy_set_header Connection $connection_upgrade;
			proxy_set_header X-Forwarded-For $remote_addr; # preserve client IP
		}
		location @{PackageName}_Fallback
		{
			proxy_pass http://{Upstream};
			proxy_http_version 1.1;
			proxy_set_header Host $host;
			proxy_set_header Upgrade $http_upgrade; # allow websockets
			proxy_set_header Connection $connection_upgrade;
			proxy_set_header X-Forwarded-For $remote_addr; # preserve client IP
		}
)---"
, R"---(
	server
	{
		listen {SSLPort};
		listen [::]:{SSLPort};
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

		location ~* "^/[a-z0-9]{40}\.(css|js)$"
		{
			gzip_static always;
			expires max;
			add_header Strict-Transport-Security "max-age=63072000; includeSubdomains; preload;" always;
			add_header Cache-Control public;
			root {StaticRoot};
			access_log logs/static_access_{PackageName}.log;
		}

{ServerRedirect_{PackageName}}

{StaticPackages}
{SubPackages}

		# pass all requests to Meteor
		location /
		{
{PathRedirect}
{ServerRootOptions_{PackageName}}
			error_page 502 = @{PackageName}_Fallback;
			proxy_pass http://{UpstreamSticky};
			proxy_http_version 1.1;
			proxy_set_header Host $host;
			proxy_set_header Upgrade $http_upgrade; # allow websockets
			proxy_set_header Connection $connection_upgrade;
			proxy_set_header X-Forwarded-For $remote_addr; # preserve client IP
		}
		location @{PackageName}_Fallback
		{
			proxy_pass http://{Upstream};
			proxy_http_version 1.1;
			proxy_set_header Host $host;
			proxy_set_header Upgrade $http_upgrade; # allow websockets
			proxy_set_header Connection $connection_upgrade;
			proxy_set_header X-Forwarded-For $remote_addr; # preserve client IP
		}

		location /robots.txt {
			return 200 "{AllowRobots}";
		}
	}
)---"
	}
;

ch8 const *g_pStaticServerTemplate[2] =
	{
R"---(
		location /{SubPath}/
		{
			gzip_static always;
			add_header Strict-Transport-Security "max-age=31536000;" always;
			add_header Cache-Control no-cache;
			root {StaticRoot};
			access_log logs/static_access_{PackageName}.log;
		}
)---"
, R"---(
	server
	{
		listen {SSLPort};
		listen [::]:{SSLPort};
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
{StaticPackages}
{SubPackages}

		location /
		{
			gzip_static always;
			add_header Strict-Transport-Security "max-age=63072000; includeSubdomains; preload;" always;
			add_header Cache-Control no-cache;
			root {StaticRoot};
			access_log logs/static_access_{PackageName}.log;
		}

		location /robots.txt {
			return 200 "{AllowRobots}";
		}
	}
)---"
	}
;

ch8 const *g_pFastCGIServerTemplate[2] =
	{
R"---(
		location ~* "^/{SubPath}/[a-z0-9]{40}\.(css|js)$"
		{
			gzip_static always;
			expires max;
			add_header Strict-Transport-Security "max-age=31536000;" always;
			add_header Cache-Control public;
			root {StaticRoot};
			access_log logs/static_access_{PackageName}.log;
		}

		# pass all requests to FastCGI
		location /{SubPath}/
		{
{PathRedirect}
{ServerRootOptions_{PackageName}}
			include {FastCGIFile};

			error_page 502 = @{PackageName}_Fallback;

			fastcgi_pass {UpstreamSticky};
			fastcgi_keep_conn on;

			gzip off;
		}
		location @{PackageName}_Fallback
		{
			include {FastCGIFile};

			fastcgi_pass {Upstream};
			fastcgi_keep_conn on;

			gzip off;
		}
)---"
, R"---(
	server
	{
		listen {SSLPort};
		listen [::]:{SSLPort};
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

		location ~* "^/[a-z0-9]{40}\.(css|js)$"
		{
			gzip_static always;
			expires max;
			add_header Strict-Transport-Security "max-age=63072000; includeSubdomains; preload;" always;
			add_header Cache-Control public;
			root {StaticRoot};
			access_log logs/static_access_{PackageName}.log;
		}

{ServerRedirect_{PackageName}}

{StaticPackages}
{SubPackages}

		# pass all requests to FastCGI
		location /
		{
{PathRedirect}
{ServerRootOptions_{PackageName}}
			include {FastCGIFile};

			error_page 502 = @{PackageName}_Fallback;

			fastcgi_pass {UpstreamSticky};
			fastcgi_keep_conn on;

			gzip off;
		}
		location @{PackageName}_Fallback
		{
			include {FastCGIFile};

			fastcgi_pass {Upstream};
			fastcgi_keep_conn on;

			gzip off;
		}

		location /robots.txt {
			return 200 "{AllowRobots}";
		}
	}
)---"
	}
;

ch8 const *g_pWebsocketServerTemplate[2] =
	{
R"---(
		# pass all requests to Websocket
		location /{SubPath}
		{
{PathRedirect}
{ServerRootOptions_{PackageName}}
			error_page 502 = @{PackageName}_Fallback;
			proxy_pass http://{UpstreamSticky};
			proxy_http_version 1.1;
			proxy_set_header Host $host;
			proxy_set_header Upgrade $http_upgrade; # allow websockets
			proxy_set_header Connection $connection_upgrade;
			proxy_set_header X-Forwarded-For $remote_addr; # preserve client IP
		}
		location @{PackageName}_Fallback
		{
			proxy_pass http://{Upstream};
			proxy_http_version 1.1;
			proxy_set_header Host $host;
			proxy_set_header Upgrade $http_upgrade; # allow websockets
			proxy_set_header Connection $connection_upgrade;
			proxy_set_header X-Forwarded-For $remote_addr; # preserve client IP
		}
)---"
, R"---(
	server
	{
		listen {SSLPort};
		listen [::]:{SSLPort};
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

{StaticPackages}
{SubPackages}

		# pass all requests to Websocket
		location /
		{
{PathRedirect}
{ServerRootOptions_{PackageName}}
			error_page 502 = @{PackageName}_Fallback;
			proxy_pass http://{UpstreamSticky};
			proxy_http_version 1.1;
			proxy_set_header Host $host;
			proxy_set_header Upgrade $http_upgrade; # allow websockets
			proxy_set_header Connection $connection_upgrade;
			proxy_set_header X-Forwarded-For $remote_addr; # preserve client IP
		}
		location @{PackageName}_Fallback
		{
			proxy_pass http://{Upstream};
			proxy_http_version 1.1;
			proxy_set_header Host $host;
			proxy_set_header Upgrade $http_upgrade; # allow websockets
			proxy_set_header Connection $connection_upgrade;
			proxy_set_header X-Forwarded-For $remote_addr; # preserve client IP
		}

		location /robots.txt {
			return 200 "{AllowRobots}";
		}
	}
)---"
	}
;

ch8 const *g_pServerSeparateStaticRootTemplate = R"---(
	server
	{
		listen {SSLPort};
		listen [::]:{SSLPort};
		server_name {ServerNameStatic} {ServerNameStaticSource};
		access_log logs/static_access_{PackageName}.log;
		client_max_body_size 10M;

		add_header 'Access-Control-Allow-Origin' 'https://{ServerName}{SSLPortRewrite}';
		add_header Strict-Transport-Security "max-age=63072000; includeSubdomains; preload;" always;
		add_header Cache-Control public;

		location ~* "^/[a-z0-9]{40}\.(css|js)$"
		{
			gzip_static always;
			expires max;
			root {StaticRoot};
		}

		location ~ "^/packages/.*\.(jpg|jpeg|png|gif|mp3|ico|pdf|svg|eot|woff|woff2|ttf|otf)$"
		{
			root {StaticRoot};
		}

		location ~ "\.(jpg|jpeg|png|gif|mp3|ico|pdf|svg|eot|woff|woff2|ttf|otf)$"
		{
			root {StaticRoot}/app;
		}

		location /robots.txt {
			return 200 "User-agent: *\\nDisallow: /";
		}
	}
)---";

	TCContinuation<void> CMeteorManagerActor::fp_SetupPrerequisites_Nginx()
	{
		TCContinuation<void> Continuation;

		struct CResults
		{
			CStr m_ConfigContents;
			TCMap<CStr, CStr> m_Redirects;
			CStr m_CertificateFile;
			CStr m_CertificateKeyFile;
			CUser m_User{"", ""};
#ifdef DPlatformFamily_Windows
			CStrSecure m_UserPassword;
#endif
			bool m_bHasDHParamFile = false;
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

		g_Dispatch(*mp_FileActors)
			>
			[
				ProgramDirectory = CFile::fs_GetProgramDirectory()
				, NginxDirectory
				, bIsStaging = mp_bIsStaging
				, Domain = mp_Domain
				, Packages = mp_Options.m_Packages
				, DhParamFile
				, ConfigFile
				, User = mp_NginxUser
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

				Results.m_User = User;
#ifdef DPlatformFamily_Windows
				fsp_SetupUser(Results.m_User, Results.m_UserPassword);
#else
				fsp_SetupUser(Results.m_User);
#endif

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
					CSSLKeySetting KeySettings = CSSLKeySettings_EC_secp384r1{};
#else
					CSSLKeySetting KeySettings = CSSLKeySettings_RSA{8192};
#endif

					TCMap<CStr, CStr> RelativeDistinguishedNames;

					RelativeDistinguishedNames["C"] = CertificateCountry;
					RelativeDistinguishedNames["L"] = CertificateLocality;
					RelativeDistinguishedNames["O"] = CertificateOrganization;
					RelativeDistinguishedNames["OU"] = CertificateOrganizationalUnit;

					TCVector<uint8> CertData;
					TCVector<uint8> CertRequestData;
					TCVector<uint8, CAllocator_HeapSecure> KeyData;
					TCVector<uint8> CaCertData;
					TCVector<uint8, CAllocator_HeapSecure> CaKeyData;

					TCVector<CStr> Subjects = fg_CreateVector<CStr>(Domain, "*." + Domain);

					CSignOptions SignOptions;
					SignOptions.m_Days = 365*20;

					if (CFile::fs_FileExists(CaCertificateFile) && CFile::fs_FileExists(CaCertificateKeyFile))
					{
						CaCertData = CFile::fs_ReadFile(CaCertificateFile);
						CaKeyData = CFile::fs_ReadFile(CaCertificateKeyFile);
					}
					else
					{
						CSSLContext::CCertificateOptions Options;
						Options.m_CommonName = fg_Format("{} CA {nfh,sj16,sf0}", ManagerName, fg_GetHighEntropyRandomInteger<uint64>());
						Options.m_RelativeDistinguishedNames = RelativeDistinguishedNames;
						Options.m_KeySetting = KeySettings;
						Options.f_MakeCA();

						SignOptions.f_AddExtension_SubjectKeyIdentifier();

						CSSLContext::fs_GenerateSelfSignedCertAndKey
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
						CFile::fs_WriteFile(CaKeyData, CaCertificateKeyFile);
					}

					CSSLContext::CCertificateOptions Options;
					Options.m_CommonName = Domain;
					Options.m_RelativeDistinguishedNames = RelativeDistinguishedNames;
					Options.m_Hostnames = Subjects;
					Options.m_KeySetting = KeySettings;


					SignOptions.m_Extensions.f_Clear();
					SignOptions.f_AddExtension_AuthorityKeyIdentifier();
					Options.f_AddExtension_BasicConstraints(false);
					Options.f_AddExtension_KeyUsage(CSSLContext::EKeyUsage_KeyEncipherment | CSSLContext::EKeyUsage_DigitalSignature);

					CSSLContext::fs_GenerateClientCertificateRequest(Options, CertRequestData, KeyData);
					CSSLContext::fs_SignClientCertificate(CaCertData, CaKeyData, CertRequestData, CertData, SignOptions);

					CFile::fs_WriteFile(CertData, Results.m_CertificateFile);
					CFile::fs_WriteFile(KeyData, Results.m_CertificateKeyFile);
					CFile::fs_WriteFile(CertRequestData, CertificateRequestFile);
				}

				if (CFile::fs_FileExists(DhParamFile))
					Results.m_bHasDHParamFile = true;

				CFile::fs_SetUnixAttributesRecursive(NginxDirectory + "/certificates", EFileAttrib_UserRead, EFileAttrib_UserRead | EFileAttrib_UserWrite | EFileAttrib_UserExecute);

				Results.m_ConfigContents = CFile::fs_ReadStringFromFile(ProgramDirectory + "/Source/Malterlib_Meteor_App_MeteorManager_Nginx.conf");
				CFile::fs_DiffCopyFileOrDirectory(ProgramDirectory + "/Source/Malterlib_Meteor_App_MeteorManager_FastCGI.conf", FastCGIFile, nullptr);

				{
					for (auto &Package : Packages)
					{
						if (!Package.f_IsServer())
							continue;

						if (Package.m_RedirectsFile.f_IsEmpty())
							continue;

						CStr RedirectPath = fg_Format("{}/{}/{}", CFile::fs_GetProgramDirectory(), Package.f_GetName(), Package.m_RedirectsFile);

						CStr RedirectContents;
						try
						{
							CJSON const RedirectJSON = CJSON::fs_FromString(CFile::fs_ReadStringFromFile(RedirectPath), RedirectPath);

							for (auto const &Redirect : RedirectJSON["redirects"].f_Array())
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
										"				add_header Set-Cookie \"{}=$http_referer; Secure; HttpOnly; Path=/; Domain=.{}\";\n"
										"				return 302 {}?campaign={};\n"
										"			}\n"
										, Path
										, ReferrerCookie
										, Domain
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

						Results.m_Redirects[Package.f_GetName()] = fg_Move(RedirectContents);
					}

				}

				return Results;
			}
			> Continuation / [=](CResults &&_Results)
			{
				CStr ProgramDirectory = CFile::fs_GetProgramDirectory();

				mp_NginxUser = _Results.m_User;
				TCContinuation<void> SavePasswordContinuation;
#ifdef DPlatformFamily_Windows
				if (!_Results.m_UserPassword.f_IsEmpty())
				{
					mp_AppState.m_StateDatabase.m_Data["Users"][mp_NginxUser.m_UserName]["Password"] = _Results.m_UserPassword;
					mp_AppState.f_SaveStateDatabase() > Continuation;
					return;
				}
				else
#endif
					SavePasswordContinuation.f_SetResult();

				CStr ConfigContents = _Results.m_ConfigContents;

				CStr PidFile = NginxDirectory + "/nginx.pid";

				TCSet<CStr> VariablesToRemove;
				VariablesToRemove["{HTTPDefaultServerLocations}"];
				VariablesToRemove["{HTTPDefaultServerLocations_www}"];
				VariablesToRemove["{HTTPSDefaultServerLocations_www}"];

				CStr UpstreamServers;
				TCMap<CStr, CStr> Upstreams;
				{
					for (auto &Package : mp_Options.m_Packages)
					{
						if (!Package.f_IsServer() || Package.f_IsStatic())
							continue;

						bool bIsFastCGI = Package.m_Type == CMeteorManagerOptions::EPackageType_FastCGI;

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
							UpstreamServers += "\t\tserver {}:8080 max_fails=30 fail_timeout=30s;\n"_f << fp_GetAppIPAddress(AppLaunch);
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
								UpstreamServers += "		{} {}:8080;\n"_f << AppLaunch.m_BackendIdentifier << fp_GetAppIPAddress(AppLaunch);
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
								UpstreamServers += "		{} {}:8080;\n"_f << AppLaunch.m_BackendIdentifier << fp_GetAppIPAddress(AppLaunch);
							}

							UpstreamServers += "	}\n\n";
						}
					}
					UpstreamServers += "{MeteorManagerUpstream}\n";
					VariablesToRemove["{MeteorManagerUpstream}"];
				}

				ConfigContents = ConfigContents.f_Replace("{MeteorManagerUpstream}", UpstreamServers);

				VariablesToRemove["{MeteorManagerHTTPServers}"];
				if (mp_Options.m_bRedirectWWW)
				{
					CStr Section = R"---(
	server
	{
		listen {Port};
		server_name www.{DomainName};

{HTTPDefaultServerLocations_www}

		location /
		{
			add_header Set-Cookie "{HTTPRedirectReferrerCookie}=$http_referer; Secure; HttpOnly; Path=/; Domain=.{DomainName}";
			return 302 https://$host{SSLPortRewrite}$request_uri;
		}
	}
{MeteorManagerHTTPServers}
)---";

					ConfigContents = ConfigContents.f_Replace("{MeteorManagerHTTPServers}", Section);
				}

				CStr Servers;
				TCMap<CStr, CStr> VariablesToReplace;
				{
					TCMap<CStr, CStr> SubPathServers;
					for (int i = 0; i < 2; ++i)
					{
						bool bIsSubPackage = i == 0;

						for (auto &Package : mp_Options.m_Packages)
						{
							if (!Package.f_IsServer())
								continue;

							if (bIsSubPackage && Package.m_SubPath.f_IsEmpty())
								continue;
							else if (!bIsSubPackage && !Package.m_SubPath.f_IsEmpty())
								continue;

							bool bIsFastCGI = Package.m_Type == CMeteorManagerOptions::EPackageType_FastCGI;
							bool bIsWebsocket = Package.m_Type == CMeteorManagerOptions::EPackageType_Websocket;
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

							if (!bIsSubPackage && bEnableSeparateStaticRoot && Package.m_Type == CMeteorManagerOptions::EPackageType_Meteor)
							{
								Server += g_pServerSeparateStaticRootTemplate;
								Server = Server.f_Replace("{ServerNameStatic}", fp_GetPackageHostname(Package.f_GetName(), EHostnamePrefix_Static));
								Server = Server.f_Replace("{ServerNameStaticSource}", fp_GetPackageHostname(Package.f_GetName(), EHostnamePrefix_StaticSource));
							}

							CStr ServerName = fp_GetPackageHostname(Package.f_GetName(), EHostnamePrefix_None);

							bool bIsMainServer = ServerName == mp_Domain && Package.m_SubPath.f_IsEmpty();

							Server = Server.f_Replace("{AllowRobots}", Package.m_bAllowRobots && mp_bAllowRobots ? "User-agent: *\\nAllow: /" : "User-agent: *\\nDisallow: /");

							if (bIsMainServer)
							{
								CStr Section = R"---(
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

								Server = Server.f_Replace("{CheckServerNameLogic}", Section);
							}
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
							{
								if (Package.m_ExternalRoot.f_IsEmpty())
									Server = Server.f_Replace("{StaticRoot}", fg_Format("{}/{}", ProgramDirectory, Package.f_GetName()));
								else
									Server = Server.f_Replace("{StaticRoot}", CFile::fs_GetExpandedPath(ProgramDirectory / Package.m_ExternalRoot));
							}
							else if (bIsFastCGI)
								Server = Server.f_Replace("{StaticRoot}", fg_Format("{}/{}/static", ProgramDirectory, Package.f_GetName()));
							else if (!bIsWebsocket)
								Server = Server.f_Replace("{StaticRoot}", fg_Format("{}/{}/programs/web.browser", ProgramDirectory, Package.f_GetName()));

							VariablesToReplace[fg_Format("{{ServerName_{}}", Package.f_GetName())] = ServerName;
							VariablesToReplace[fg_Format("{{ServerNameEscaped_{}}", Package.f_GetName())] = ServerName.f_Replace(".", "\\.");

							VariablesToRemove[("{{ServerNameExtra_{}}"_f << Package.f_GetName()).f_GetStr()];
							VariablesToRemove[("{{ServerAccessCheck_{}}"_f << Package.f_GetName()).f_GetStr()];
							VariablesToRemove[("{{ServerRedirect_{}}"_f << Package.f_GetName()).f_GetStr()];
							VariablesToRemove[("{{ServerRootOptions_{}}"_f << Package.f_GetName()).f_GetStr()];

							CStr StaticPackages;

							if (bIsMainServer)
							{
								for (auto &Package : mp_Options.m_Packages)
								{
									if (Package.f_IsDynamicServer() || Package.m_StaticPath.f_IsEmpty())
										continue;

									StaticPackages += "		location {}\n"_f << Package.m_StaticPath;
									StaticPackages += "		{\n";
									StaticPackages += "			alias {}/{};\n"_f << ProgramDirectory << Package.f_GetName();
									if (Package.f_IsStatic())
										StaticPackages += "			gzip_static always;\n";
									StaticPackages += "			add_header Strict-Transport-Security \"max-age=63072000; includeSubdomains; preload;\" always;\n";
									StaticPackages += "			add_header Cache-Control no-cache;\n";
									StaticPackages += "			access_log logs/static_access_{}.log;\n"_f << Package.f_GetName();
									StaticPackages += "		}\n";
								}
							}

							Server = Server.f_Replace("{StaticPackages}", StaticPackages);
							if (!bIsSubPackage)
								Server = Server.f_Replace("{SubPackages}", SubPathServers[ServerName]);
							Server = Server.f_Replace("{PathRedirect}",  _Results.m_Redirects[Package.f_GetName()]);

							if (bIsMainServer && mp_Options.m_bRedirectWWW)
							{
								Server += R"---(
	server
	{
		listen {SSLPort};
		listen [::]:{SSLPort};
		server_name www.{DomainName};

{HTTPSDefaultServerLocations_www}

		location /
		{
			add_header Strict-Transport-Security "max-age=63072000; includeSubdomains; preload;" always;
			add_header Set-Cookie "{HTTPRedirectReferrerCookie}=$http_referer; Secure; HttpOnly; Path=/; Domain=.{DomainName}";
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
					Servers += "{MeteorManagerServers}\n";
					VariablesToRemove["{MeteorManagerServers}"];
				}
				ConfigContents = ConfigContents.f_Replace("{MeteorManagerServers}", Servers);

				if (mp_pCustomization)
				{
					mp_pCustomization->f_ManipulateNginxConfig
						(
							ConfigContents
							, mp_AppState
							, mp_Options
							, [&](CStr const &_Name, CEJSON const &_Default) -> CEJSON
							{
								return fp_GetConfigValue(_Name, _Default);
							}
							, mp_Tags
						)
					;
				}

				for (auto &ToRemove : VariablesToRemove)
					ConfigContents = ConfigContents.f_Replace(ToRemove, "");

				{
					CStr RedirectReferrerCookie = mp_Options.m_HTTPRedirectReferrerCookie.f_IsEmpty() ? "RedirectReferrer" : mp_Options.m_HTTPRedirectReferrerCookie;
					ConfigContents = ConfigContents.f_Replace("{HTTPRedirectReferrerCookie}", RedirectReferrerCookie);
				}

				for (auto &ReplaceWith : VariablesToReplace)
					ConfigContents = ConfigContents.f_Replace(VariablesToReplace.fs_GetKey(ReplaceWith), ReplaceWith);

				ConfigContents = ConfigContents.f_Replace("{DomainName}", mp_Domain);
				ConfigContents = ConfigContents.f_Replace("{DomainNameEscaped}", mp_Domain.f_Replace(".", "\\."));

				ConfigContents = ConfigContents.f_Replace("{Root}", NginxDirectory + "/root");
				ConfigContents = ConfigContents.f_Replace("{Port}", CStr::fs_ToStr(mp_WebPort));
				ConfigContents = ConfigContents.f_Replace("{SSLPort}", CStr::fs_ToStr(mp_WebSSLPort));
				if (mp_WebSSLPort == 443)
					ConfigContents = ConfigContents.f_Replace("{SSLPortRewrite}", "");
				else
					ConfigContents = ConfigContents.f_Replace("{SSLPortRewrite}", ":" + CStr::fs_ToStr(mp_WebSSLPort));
				ConfigContents = ConfigContents.f_Replace("{Certificate}", _Results.m_CertificateFile);
				ConfigContents = ConfigContents.f_Replace("{CertificateKey}", _Results.m_CertificateKeyFile);
				ConfigContents = ConfigContents.f_Replace("{PidFile}", PidFile);
				ConfigContents = ConfigContents.f_Replace("{WorkerMaxOpenedFiles}", CStr::fs_ToStr(fs_GetNginxWorkerFileLimits()));
				
#ifdef DPlatformFamily_Windows
				ConfigContents = ConfigContents.f_Replace("{NgnixUserLine}", "");
#else
				ConfigContents = ConfigContents.f_Replace("{NgnixUserLine}", ("user {} {};"_f << mp_NginxUser.m_UserName << mp_NginxUser.m_GroupName).f_GetStr());
#endif

				{
					if (_Results.m_bHasDHParamFile)
						ConfigContents = ConfigContents.f_Replace("{ssl_dhparam}", fg_Format("ssl_dhparam {};", DhParamFile));
					else
						ConfigContents = ConfigContents.f_Replace("{ssl_dhparam}", "");
				}

				{
					CStr RobotsTxtContents;

					if (fp_GetConfigValue("AllowRobots", false).f_Boolean())
						RobotsTxtContents = "User-agent: *\\nAllow: /";
					else
						RobotsTxtContents = "User-agent: *\\nDisallow: /";

					ConfigContents = ConfigContents.f_Replace("{AllowRobots}", RobotsTxtContents);
				}

				{
					CStr ContentTypesContents;
					for (auto &Extensions : CMeteorManagerActor::fsp_GetContentTypes())
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



				(g_Dispatch(*mp_FileActors) > [ConfigFile, ConfigContents, UserName = mp_NginxUser.m_UserName, GroupName = mp_NginxUser.m_GroupName, NginxDirectory]()
					{
						CFile::fs_WriteStringToFile(ConfigFile, ConfigContents, false);

						CFile::fs_SetOwnerAndGroupRecursive(NginxDirectory, UserName, GroupName);
						CFile::fs_SetOwnerAndGroupRecursive(NginxDirectory + "/certificates", "root", GroupName);
					})
					+ SavePasswordContinuation
					> Continuation / [Continuation]()
					{
						Continuation.f_SetResult();
					}
				;
			}
		;
		return Continuation;
	}

	TCContinuation<void> CMeteorManagerActor::fp_StartNginx()
	{
		if (mp_NginxLaunch || mp_bStopped || mp_bDestroyed || !mp_bStartNgnix)
			return fg_Explicit(); // Launch already in progress

		CStr ProgramDirectory = CFile::fs_GetProgramDirectory();
		CStr NginxDirectory = fp_GetDataPath("nginx");
		CStr NginxConfig = NginxDirectory + "/nginx.conf";

		TCVector<CStr> Arguments;
		Arguments.f_Insert("-c");
		Arguments.f_Insert(NginxConfig);

		TCContinuation<void> Continuation;

		CProcessLaunchActor::CLaunch Launch = CProcessLaunchParams::fs_LaunchExecutable
			(
				ProgramDirectory + "/bin/nginx"
				, Arguments
				, NginxDirectory
				, [this, Continuation](CProcessLaunchStateChangeVariant const &_Change, fp64 _TimeSinceStart)
				{
					switch (_Change.f_GetTypeID())
					{
					case EProcessLaunchState_Launched:
						{
							if (mp_bStopped || mp_bDestroyed)
							{
								if (mp_NginxLaunch)
									mp_NginxLaunch(&CProcessLaunchActor::f_StopProcess) > fg_DiscardResult();
								Continuation.f_SetException(DMibErrorInstance("Application is being destroyed"));
							}
							else
								Continuation.f_SetResult();
						}
						break;
					case EProcessLaunchState_Exited:
						{
							if (!mp_bStopped && !mp_bDestroyed)
							{
								DLogWithCategory(nginx, Info, "Scheduling relaunch of nginx in 10 seconds");
								fg_Timeout(10.0) > [this]
									{
										if (!mp_bStopped && !mp_bDestroyed)
											fp_StartNginx();
									}
								;
							}
							mp_NginxLaunch.f_Clear();
							mp_NginxLaunchSubscription.f_Clear();
						}
						break;
					case EProcessLaunchState_LaunchFailed:
						{
							Continuation.f_SetException(DMibErrorInstance(fg_Format("nginx launch failed: {}", _Change.f_Get<EProcessLaunchState_LaunchFailed>())));
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
			Limit.m_Value = fs_GetNginxFileLimits(mp_AppLaunches.f_GetLen());
			Limit.m_MaxValue = Limit.m_Value;
		}

		fs_SetupEnvironment(Params);
		Params.m_bMergeEnvironment = true;

		Params.m_Environment["HOME"] = NginxDirectory;
		Params.m_Environment["TMPDIR"] = NginxDirectory + "/.tmp";
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

		mp_NginxLaunch(&CProcessLaunchActor::f_Launch, fg_Move(Launch), fg_ThisActor(this)) > [this, Continuation](TCAsyncResult<CActorSubscription> &&_Subscription)
			{
				if (!_Subscription)
				{
					Continuation.f_SetException(fg_Move(_Subscription));
					mp_NginxLaunch.f_Clear();
					return;
				}
				mp_NginxLaunchSubscription = fg_Move(*_Subscription);
			}
		;

		return Continuation;
	}
}
