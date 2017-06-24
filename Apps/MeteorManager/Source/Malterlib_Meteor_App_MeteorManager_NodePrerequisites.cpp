// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Meteor_App_MeteorManager_Server.h"

#include <Mib/File/ExeFS>
#include <Mib/File/VirtualFS>
#include <Mib/File/VirtualFSs/MalterlibFS>
#include <Mib/Encoding/JSONShortcuts>

namespace NMib::NMeteor::NMeteorManager
{
	mint CMeteorManagerActor::fp_GetNumNodes() const
	{
		mint nNodes = 0;
		for (auto &Package : mp_Options.m_Packages)
			nNodes += Package.m_Concurrency;
		return nNodes;
	}
	
	TCContinuation<void> CMeteorManagerActor::fp_SetupPrerequisites_OSSetup()
	{
		CStr ProgramDirectory = CFile::fs_GetProgramDirectory();
		CStr SetupOSFile = ProgramDirectory + "/Source/Malterlib_Meteor_App_MeteorManager_OSSetup.sh";

		TCMap<CStr, CStr> Environment;
		Environment["NumNodeServers"] = fg_Format("{}", fp_GetNumNodes());
		Environment["PlatformFamily"] = DMibStringize(DPlatformFamily);
		Environment["LoopbackPrefix"] = fg_Format("{}", mp_Options.m_LoopbackPrefix);

		TCContinuation<void> Continuation;
		f_LaunchTool(SetupOSFile, ProgramDirectory, {}, "OSSetup", ELogVerbosity_Errors, Environment) > Continuation.f_ReceiveAny();
		return Continuation;
	}
	
	CHashDigest_MD5 CMeteorManagerActor::fsp_GetFileChecksum(CStr const &_File)
	{
		CStr ChecksumFileName = _File + ".md5";
		if (CFile::fs_FileExists(ChecksumFileName))
			return CHashDigest_MD5::fs_FromString(CFile::fs_ReadStringFromFile(ChecksumFileName).f_Left(32));
		return CFile::fs_GetFileChecksum(_File);
	}

	TCContinuation<void> CMeteorManagerActor::fp_SetupPrerequisites_NodeExtract()
	{
		CStr ProgramDirectory = CFile::fs_GetProgramDirectory();
		CStr NodeDirectory = fp_GetDataPath("node");
		
		struct CNodeInfo
		{
			CUser m_User = {""};
			bool m_bForceAppReinstall = false;
		};
	
		TCContinuation<void> Continuation;

		g_Dispatch(*mp_FileActors) > [ProgramDirectory, NodeDirectory, ThisActor = fg_ThisActor(this), NodeUser = mp_NodeUser]() mutable -> TCContinuation<CNodeInfo>
			{
				DLog(Info, "Extracting node distribution");
				
				TCContinuation<CNodeInfo> Continuation;
				
				CStr DistFile;

				bool bDoInstall = false;
				bool bForceAppReinstall = false;
				CStr DistDirectory = ProgramDirectory + "/node_dist";
				CStr ChecksumFileName = ProgramDirectory + "/node.installed.md5";
				CStr NewChecksum;

				try
				{
					fsp_SetupUser(NodeUser);
					
					auto Files = CFile::fs_FindFiles(ProgramDirectory + "/node-*");
					
					if (!Files.f_IsEmpty())
					{
						DistFile = Files[0];

						CStr TmpDirectory = NodeDirectory + "/.tmp";
						CFile::fs_CreateDirectory(NodeDirectory);

						if (CFile::fs_FileExists(TmpDirectory))
							CFile::fs_DeleteDirectoryRecursive(TmpDirectory);

						CFile::fs_CreateDirectory(TmpDirectory);

						CStr NodeCertificateDirectory = NodeDirectory + "/certificates";
						
						if (CFile::fs_FileExists(NodeCertificateDirectory))
						{
							CFile::fs_SetUnixAttributesRecursive
								(
									NodeCertificateDirectory
									, NFile::EFileAttrib_UserRead, NFile::EFileAttrib_UserRead | NFile::EFileAttrib_UserExecute
									, false
								)
							;
						}

						CFile::fs_SetOwnerAndGroupRecursive(NodeDirectory, NodeUser.m_Name, NodeUser.m_Name);

						NewChecksum = fsp_GetFileChecksum(DistFile).f_GetString();
						
						if (CFile::fs_FileExists(DistDirectory))
						{
							CStr OldChecksum;

							if (CFile::fs_FileExists(ChecksumFileName))
								OldChecksum = CFile::fs_ReadStringFromFile(ChecksumFileName, true);

							if (NewChecksum != OldChecksum)
							{
								DLog
									(
										Info
										, "New node distribution detected with checksum '{}' that differs from installed checksum '{}'. Installing new distribution"
										, NewChecksum
										, OldChecksum
									)
								;
								CFile::fs_DeleteDirectoryRecursive(DistDirectory);

								bForceAppReinstall = true;
								bDoInstall = true;
							}
							else
								DLog(Info, "Installed node distribution with checksum '{}' is up to date", NewChecksum, OldChecksum);
						}
						else
						{
							DLog(Info, "No node distribution installed, installing distribution with checksum '{}'", NewChecksum);
							if (CFile::fs_FileExists(ChecksumFileName))
								CFile::fs_DeleteFile(ChecksumFileName); // Make sure of retry if failure
							bDoInstall = true;
						}
					}
					else
					{
						DLog(Error, "No node distribution found");
						
						CNodeInfo NodeInfo;
						NodeInfo.m_User = NodeUser;
						NodeInfo.m_bForceAppReinstall = bForceAppReinstall;

						Continuation.f_SetResult(NodeInfo);
						return Continuation;
					}
				}
				catch (NException::CException const &)
				{
					Continuation.f_SetCurrentException();
					return Continuation;
				}
				
				if (bDoInstall)
				{
					ThisActor
						(
							&CMeteorManagerActor::f_LaunchTool
							, CStr{"tar"}
							, ProgramDirectory
							, fg_CreateVector<CStr>("--no-same-owner", "-xf", DistFile)
							, CStr{"ExtractNode"}
							, ELogVerbosity_All
							, fg_Default()
							, true
							, fg_Default()
							, fg_Default()
						)
						> Continuation / [=]
						{
							try
							{
								CFile::fs_RenameFile(ProgramDirectory + "/" + CFile::fs_GetFileNoExt(CFile::fs_GetFileNoExt(DistFile)), DistDirectory);

								CFile::fs_SetOwnerAndGroupRecursive
									(
										DistDirectory
										, NSys::fg_UserManagement_GetProcessRealUserName()
										, NSys::fg_UserManagement_GetProcessRealGroupName()
									)
								;
								CFile::fs_WriteStringToFile(ChecksumFileName, NewChecksum, false);
							}
							catch (NException::CException const &)
							{
								Continuation.f_SetCurrentException();
								return;
							}
							
							CNodeInfo NodeInfo;
							NodeInfo.m_User = NodeUser;
							NodeInfo.m_bForceAppReinstall = bForceAppReinstall;

							Continuation.f_SetResult(NodeInfo);
						}
					;
				}
				else
				{
					CNodeInfo NodeInfo;
					NodeInfo.m_User = NodeUser;
					NodeInfo.m_bForceAppReinstall = bForceAppReinstall;

					Continuation.f_SetResult(NodeInfo);
					return Continuation;
				}
				
				return Continuation;
			}
			> Continuation / [this, Continuation](CNodeInfo const &_NodeInfo)
			{
				mp_NodeUser = _NodeInfo.m_User;
				Continuation.f_SetResult();
			}
		;

		return Continuation;
	}
	
	TCContinuation<void> CMeteorManagerActor::fp_SetupPrerequisites_Node()
	{
		TCContinuation<void> Continuation;
		fp_SetupPrerequisites_OSSetup()
			+ fp_SetupPrerequisites_NodeExtract()
			> Continuation / [Continuation]
			{
				Continuation.f_SetResult();
			}
		;
		return Continuation;
	}
}
