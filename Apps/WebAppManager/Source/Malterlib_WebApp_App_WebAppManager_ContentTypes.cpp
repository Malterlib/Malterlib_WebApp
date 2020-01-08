
#include "Malterlib_WebApp_App_WebAppManager_Server.h"

namespace NMib::NWebApp::NWebAppManager
{
	TCMap<CStr, TCVector<CStr>> g_DefaultContentTypes =
		{
			{"text/html", TCVector<CStr>{"html", "htm", "shtml"}}
			, {"text/css", TCVector<CStr>{"css"}}
			, {"text/xml", TCVector<CStr>{"xml"}}
			, {"image/gif", TCVector<CStr>{"gif"}}
			, {"image/jpeg", TCVector<CStr>{"jpeg", "jpg"}}
			, {"application/javascript", TCVector<CStr>{"js"}}
			, {"application/atom+xml", TCVector<CStr>{"atom"}}
			, {"application/rss+xml", TCVector<CStr>{"rss"}}

			, {"text/mathml", TCVector<CStr>{"mml"}}
			, {"text/plain", TCVector<CStr>{"txt"}}
			, {"text/vnd.sun.j2me.app-descriptor", TCVector<CStr>{"jad"}}
			, {"text/vnd.wap.wml", TCVector<CStr>{"wml"}}
			, {"text/x-component", TCVector<CStr>{"htc"}}
			, {"text/vnd.yaml", TCVector<CStr>{"yml"}}

			, {"image/png", TCVector<CStr>{"png"}}
			, {"image/tiff", TCVector<CStr>{"tif", "tiff"}}
			, {"image/vnd.wap.wbmp", TCVector<CStr>{"wbmp"}}
			, {"image/x-icon", TCVector<CStr>{"ico"}}
			, {"image/x-jng", TCVector<CStr>{"jng"}}
			, {"image/x-ms-bmp", TCVector<CStr>{"bmp"}}
			, {"image/svg+xml", TCVector<CStr>{"svg", "svgz"}}
			, {"image/webp", TCVector<CStr>{"webp"}}

			, {"application/x-font-ttf", TCVector<CStr>{"ttc", "ttf"}}
			, {"application/x-font-otf", TCVector<CStr>{"otf"}}
			, {"application/font-woff", TCVector<CStr>{"woff"}}
			, {"application/font-woff2", TCVector<CStr>{"woff2"}}
			, {"application/vnd.ms-fontobject", TCVector<CStr>{"eot"}}

			, {"application/java-archive", TCVector<CStr>{"jar", "war", "ear"}}
			, {"application/json", TCVector<CStr>{"json"}}
			, {"application/mac-binhex40", TCVector<CStr>{"hqx"}}
			, {"application/msword", TCVector<CStr>{"doc"}}
			, {"application/pdf", TCVector<CStr>{"pdf"}}
			, {"application/postscript", TCVector<CStr>{"ps", "eps", "ai"}}
			, {"application/rtf", TCVector<CStr>{"rtf"}}
			, {"application/vnd.apple.mpegurl", TCVector<CStr>{"m3u8"}}
			, {"application/vnd.ms-excel", TCVector<CStr>{"xls"}}
			, {"application/vnd.ms-powerpoint", TCVector<CStr>{"ppt"}}
			, {"application/vnd.wap.wmlc", TCVector<CStr>{"wmlc"}}
			, {"application/vnd.google-earth.kml+xml", TCVector<CStr>{"kml"}}
			, {"application/vnd.google-earth.kmz", TCVector<CStr>{"kmz"}}
			, {"application/x-7z-compressed", TCVector<CStr>{"7z"}}
			, {"application/x-cocoa", TCVector<CStr>{"cco"}}
			, {"application/x-java-archive-diff", TCVector<CStr>{"jardiff"}}
			, {"application/x-java-jnlp-file", TCVector<CStr>{"jnlp"}}
			, {"application/x-makeself", TCVector<CStr>{"run"}}
			, {"application/x-perl", TCVector<CStr>{"pl", "pm"}}
			, {"application/x-pilot", TCVector<CStr>{"prc", "pdb"}}
			, {"application/x-rar-compressed", TCVector<CStr>{"rar"}}
			, {"application/x-redhat-package-manager", TCVector<CStr>{"rpm"}}
			, {"application/x-sea", TCVector<CStr>{"sea"}}
			, {"application/x-shockwave-flash", TCVector<CStr>{"swf"}}
			, {"application/x-stuffit", TCVector<CStr>{"sit"}}
			, {"application/x-tcl", TCVector<CStr>{"tcl", "tk"}}
			, {"application/x-x509-ca-cert", TCVector<CStr>{"der", "pem", "crt"}}
			, {"application/x-xpinstall", TCVector<CStr>{"xpi"}}
			, {"application/xhtml+xml", TCVector<CStr>{"xhtml"}}
			, {"application/xspf+xml", TCVector<CStr>{"xspf"}}
			, {"application/zip", TCVector<CStr>{"zip"}}

			, {"application/octet-stream", TCVector<CStr>{"bin", "exe", "dll", "deb", "dmg", "iso", "img", "msi", "msp", "msm", "gz", "tar", "AppImage"}}

			, {"application/vnd.openxmlformats-officedocument.wordprocessingml.document", TCVector<CStr>{"docx"}}
			, {"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", TCVector<CStr>{"xlsx"}}
			, {"application/vnd.openxmlformats-officedocument.presentationml.presentation", TCVector<CStr>{"pptx"}}

			, {"audio/midi", TCVector<CStr>{"mid", "midi", "kar"}}
			, {"audio/mpeg", TCVector<CStr>{"mp3"}}
			, {"audio/ogg", TCVector<CStr>{"ogg"}}
			, {"audio/x-m4a", TCVector<CStr>{"m4a"}}
			, {"audio/x-realaudio", TCVector<CStr>{"ra"}}

			, {"video/3gpp", TCVector<CStr>{"3gpp", "3gp"}}
			, {"video/mp2t", TCVector<CStr>{"ts"}}
			, {"video/mp4", TCVector<CStr>{"mp4"}}
			, {"video/mpeg", TCVector<CStr>{"mpeg", "mpg"}}
			, {"video/quicktime", TCVector<CStr>{"mov"}}
			, {"video/webm", TCVector<CStr>{"webm"}}
			, {"video/x-flv", TCVector<CStr>{"flv"}}
			, {"video/x-m4v", TCVector<CStr>{"m4v"}}
			, {"video/x-mng", TCVector<CStr>{"mng"}}
			, {"video/x-ms-asf", TCVector<CStr>{"asx", "asf"}}
			, {"video/x-ms-wmv", TCVector<CStr>{"wmv"}}
			, {"video/x-msvideo", TCVector<CStr>{"avi"}}
		}
	;

	namespace
	{
		TCMap<CStr, CStr> fg_ExtensionToContetTypes(TCMap<CStr, TCVector<CStr>> const &_Types)
		{
			TCMap<CStr, CStr> Result;

			for (auto &Extensions : _Types)
			{
				auto &Type = _Types.fs_GetKey(Extensions);
				for (auto &Extension : Extensions)
					Result[Extension] = Type;
			}

			return Result;
		}
	}

	TCMap<CStr, CStr> g_ExtensionToContentType = fg_ExtensionToContetTypes(g_DefaultContentTypes);

	TCMap<CStr, TCVector<CStr>> CWebAppManagerActor::fsp_GetContentTypes()
	{
		return g_DefaultContentTypes;
	}

	CStr CWebAppManagerActor::fsp_GetContentTypeForExtension(CStr const &_Extension)
	{
		auto pContentType = g_ExtensionToContentType.f_FindEqual(_Extension);
		if (!pContentType)
			return "";
		return *pContentType;
	}
}
