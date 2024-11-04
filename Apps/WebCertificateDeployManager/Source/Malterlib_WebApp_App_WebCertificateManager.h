// Copyright © 2020 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>
#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Concurrency/DistributedDaemon>
#include <Mib/Concurrency/Actor/Timer>
#include <Mib/Cryptography/Certificate>
#include <Mib/WebApp/WebCertificateDeploy>

namespace NMib::NWebApp::NWebCertificateManager
{
	struct CWebCertificateManagerActor : public CDistributedAppActor
	{
		CWebCertificateManagerActor();
		~CWebCertificateManagerActor();

	private:

		struct CCertificateLocation
		{
			auto f_Tuple() const;
			bool operator == (CCertificateLocation const &_Right) const;

			CStr m_Key;
			CStr m_FullChain;

		};

		struct CCertificateFileSettings
		{
			auto f_Tuple() const;
			bool operator == (CCertificateFileSettings const &_Right) const;

			CStr m_User;
			CStr m_Group;
			EFileAttrib m_Attributes = EFileAttrib_UnixAttributesValid | EFileAttrib_UserRead | EFileAttrib_UserWrite;
		};

		struct CDomainSettings
		{
			auto f_Tuple() const;
			bool operator == (CDomainSettings const &_Right) const;

			TCOptional<CCertificateLocation> m_Location_Ec;
			TCOptional<CCertificateLocation> m_Location_Rsa;
			TCOptional<CStr> m_Location_NginxPid;

			CCertificateFileSettings m_FileSettings_Certificate;
			CCertificateFileSettings m_FileSettings_Key;
		};

		struct CDomain
		{
			CStr const &f_GetName() const
			{
				return TCMap<CStr, CDomain>::fs_GetKey(*this);
			}

			CDomainSettings m_Settings;
			TCMap<CHostInfo, CWebCertificateDeployActor::CDomainStatus> m_Statuses;
			CActorSubscription m_CertificateDeploySubscription;
		};

		void fp_BuildCommandLine(CDistributedAppCommandLineSpecification &o_CommandLine) override;

		TCFuture<void> fp_StartApp(NEncoding::CEJSONSorted const _Params) override;
		TCFuture<void> fp_StopApp() override;

		TCFuture<uint32> fp_CommandLine_DomainList(CEJSONSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine);
		TCFuture<uint32> fp_CommandLine_DomainAdd(CEJSONSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine);
		TCFuture<uint32> fp_CommandLine_DomainChangeSettings(CEJSONSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine);
		TCFuture<uint32> fp_CommandLine_DomainRemove(CEJSONSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine);

		TCFuture<void> fp_ReadState();
		void fp_ParseSettings(CEJSONSorted const &_Params, CDomainSettings &o_Settings);
		void fp_ParseCommandLineSettings(CEJSONSorted const &_Params, CDomainSettings &o_Settings);
		EFileAttrib fsp_ParseAttributes(CEJSONSorted const &_JSON, EFileAttrib _OriginalAttribs);
		CEJSONSorted fsp_GenerateAttributes(EFileAttrib _Attributes);
		CEJSONSorted fp_SaveSettings(CDomainSettings const &_Settings);
		void fp_SaveState(CDomain const &_Domain);

		CEJSONSorted fp_GetConfigValue(CStr const &_Name, CEJSONSorted const &_Default) const;

		TCFuture<void> fp_UpdateDomainSettings(CStr _DomainName);

		TCMap<CStr, CDomain> mp_Domains;

		TCActor<CWebCertificateDeployActor> mp_CertificateDeployActor;
	};
}
