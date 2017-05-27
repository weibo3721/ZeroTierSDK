/*
 * ZeroTier One - Network Virtualization Everywhere
 * Copyright (C) 2011-2016  ZeroTier, Inc.  https://www.zerotier.com/
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../node/Constants.hpp"
#include "../version.h"

#ifdef __WINDOWS__
#include <WinSock2.h>
#include <Windows.h>
#include <ShlObj.h>
#include <netioapi.h>
#include <iphlpapi.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <ifaddrs.h>
#endif

#include "SoftwareUpdater.hpp"

#include "../node/Utils.hpp"
#include "../node/SHA512.hpp"
#include "../node/Buffer.hpp"
#include "../node/Node.hpp"

#include "../osdep/OSUtils.hpp"

#ifndef ZT_BUILD_ARCHITECTURE
#define ZT_BUILD_ARCHITECTURE 0
#endif
#ifndef ZT_BUILD_PLATFORM
#define ZT_BUILD_PLATFORM 0
#endif

namespace ZeroTier {

SoftwareUpdater::SoftwareUpdater(Node &node,const std::string &homePath) :
	_node(node),
	_lastCheckTime(0),
	_homePath(homePath),
	_channel(ZT_SOFTWARE_UPDATE_DEFAULT_CHANNEL),
	_distLog((FILE *)0),
	_latestValid(false),
	_downloadLength(0)
{
	// Check for a cached newer update. If there's a cached update that is not newer or looks bad, delete.
	try {
		std::string buf;
		if (OSUtils::readFile((_homePath + ZT_PATH_SEPARATOR_S ZT_SOFTWARE_UPDATE_META_FILENAME).c_str(),buf)) {
			nlohmann::json meta = OSUtils::jsonParse(buf);
			buf = std::string();
			const unsigned int rvMaj = (unsigned int)OSUtils::jsonInt(meta[ZT_SOFTWARE_UPDATE_JSON_VERSION_MAJOR],0);
			const unsigned int rvMin = (unsigned int)OSUtils::jsonInt(meta[ZT_SOFTWARE_UPDATE_JSON_VERSION_MINOR],0);
			const unsigned int rvRev = (unsigned int)OSUtils::jsonInt(meta[ZT_SOFTWARE_UPDATE_JSON_VERSION_REVISION],0);
			const unsigned int rvBld = (unsigned int)OSUtils::jsonInt(meta[ZT_SOFTWARE_UPDATE_JSON_VERSION_BUILD],0);
			if ((Utils::compareVersion(rvMaj,rvMin,rvRev,rvBld,ZEROTIER_ONE_VERSION_MAJOR,ZEROTIER_ONE_VERSION_MINOR,ZEROTIER_ONE_VERSION_REVISION,ZEROTIER_ONE_VERSION_BUILD) > 0)&&
			    (OSUtils::readFile((_homePath + ZT_PATH_SEPARATOR_S ZT_SOFTWARE_UPDATE_BIN_FILENAME).c_str(),buf))) {
				if ((uint64_t)buf.length() == OSUtils::jsonInt(meta[ZT_SOFTWARE_UPDATE_JSON_UPDATE_SIZE],0)) {
					_latestMeta = meta;
					_latestValid = true;
					//printf("CACHED UPDATE IS NEWER AND LOOKS GOOD\n");
				}
			}
		}
	} catch ( ... ) {} // exceptions indicate invalid cached update
	if (!_latestValid) {
		OSUtils::rm((_homePath + ZT_PATH_SEPARATOR_S ZT_SOFTWARE_UPDATE_META_FILENAME).c_str());
		OSUtils::rm((_homePath + ZT_PATH_SEPARATOR_S ZT_SOFTWARE_UPDATE_BIN_FILENAME).c_str());
	}
}

SoftwareUpdater::~SoftwareUpdater()
{
	if (_distLog)
		fclose(_distLog);
}

void SoftwareUpdater::setUpdateDistribution(bool distribute)
{
	_dist.clear();
	if (distribute) {
		_distLog = fopen((_homePath + ZT_PATH_SEPARATOR_S "update-dist.log").c_str(),"a");

		const std::string udd(_homePath + ZT_PATH_SEPARATOR_S "update-dist.d");
		const std::vector<std::string> ud(OSUtils::listDirectory(udd.c_str()));
		for(std::vector<std::string>::const_iterator u(ud.begin());u!=ud.end();++u) {
			// Each update has a companion .json file describing it. Other files are ignored.
			if ((u->length() > 5)&&(u->substr(u->length() - 5,5) == ".json")) {

				std::string buf;
				if (OSUtils::readFile((udd + ZT_PATH_SEPARATOR_S + *u).c_str(),buf)) {
					try {
						_D d;
						d.meta = OSUtils::jsonParse(buf); // throws on invalid JSON

						// If update meta is called e.g. foo.exe.json, then foo.exe is the update itself
						const std::string binPath(udd + ZT_PATH_SEPARATOR_S + u->substr(0,u->length() - 5));
						const std::string metaHash(OSUtils::jsonBinFromHex(d.meta[ZT_SOFTWARE_UPDATE_JSON_UPDATE_HASH]));
						if ((metaHash.length() == ZT_SHA512_DIGEST_LEN)&&(OSUtils::readFile(binPath.c_str(),d.bin))) {
							uint8_t sha512[ZT_SHA512_DIGEST_LEN];
							SHA512::hash(sha512,d.bin.data(),(unsigned int)d.bin.length());
							if (!memcmp(sha512,metaHash.data(),ZT_SHA512_DIGEST_LEN)) { // double check that hash in JSON is correct
								d.meta[ZT_SOFTWARE_UPDATE_JSON_UPDATE_SIZE] = d.bin.length(); // override with correct value -- setting this in meta json is optional
								_dist[Array<uint8_t,16>(sha512)] = d;
								if (_distLog) {
									fprintf(_distLog,".......... INIT: DISTRIBUTING %s (%u bytes)" ZT_EOL_S,binPath.c_str(),(unsigned int)d.bin.length());
									fflush(_distLog);
								}
							}
						}
					} catch ( ... ) {} // ignore bad meta JSON, etc.
				}

			}
		}
	} else {
		if (_distLog) {
			fclose(_distLog);
			_distLog = (FILE *)0;
		}
	}
}

void SoftwareUpdater::handleSoftwareUpdateUserMessage(uint64_t origin,const void *data,unsigned int len)
{
	if (!len) return;
	const MessageVerb v = (MessageVerb)reinterpret_cast<const uint8_t *>(data)[0];
	try {
		switch(v) {

			case VERB_GET_LATEST:
			case VERB_LATEST: {
				nlohmann::json req = OSUtils::jsonParse(std::string(reinterpret_cast<const char *>(data) + 1,len - 1)); // throws on invalid JSON
				if (req.is_object()) {
					const unsigned int rvMaj = (unsigned int)OSUtils::jsonInt(req[ZT_SOFTWARE_UPDATE_JSON_VERSION_MAJOR],0);
					const unsigned int rvMin = (unsigned int)OSUtils::jsonInt(req[ZT_SOFTWARE_UPDATE_JSON_VERSION_MINOR],0);
					const unsigned int rvRev = (unsigned int)OSUtils::jsonInt(req[ZT_SOFTWARE_UPDATE_JSON_VERSION_REVISION],0);
					const unsigned int rvBld = (unsigned int)OSUtils::jsonInt(req[ZT_SOFTWARE_UPDATE_JSON_VERSION_BUILD],0);
					const unsigned int rvPlatform = (unsigned int)OSUtils::jsonInt(req[ZT_SOFTWARE_UPDATE_JSON_PLATFORM],0);
					const unsigned int rvArch = (unsigned int)OSUtils::jsonInt(req[ZT_SOFTWARE_UPDATE_JSON_ARCHITECTURE],0);
					const unsigned int rvVendor = (unsigned int)OSUtils::jsonInt(req[ZT_SOFTWARE_UPDATE_JSON_VENDOR],0);
					const std::string rvChannel(OSUtils::jsonString(req[ZT_SOFTWARE_UPDATE_JSON_CHANNEL],""));

					if (v == VERB_GET_LATEST) {

						if (_dist.size() > 0) {
							const nlohmann::json *latest = (const nlohmann::json *)0;
							const std::string expectedSigner = OSUtils::jsonString(req[ZT_SOFTWARE_UPDATE_JSON_EXPECT_SIGNED_BY],"");
							unsigned int bestVMaj = rvMaj;
							unsigned int bestVMin = rvMin;
							unsigned int bestVRev = rvRev;
							unsigned int bestVBld = rvBld;
							for(std::map< Array<uint8_t,16>,_D >::const_iterator d(_dist.begin());d!=_dist.end();++d) {
								if ((OSUtils::jsonInt(d->second.meta[ZT_SOFTWARE_UPDATE_JSON_PLATFORM],0) == rvPlatform)&&
								    (OSUtils::jsonInt(d->second.meta[ZT_SOFTWARE_UPDATE_JSON_ARCHITECTURE],0) == rvArch)&&
								    (OSUtils::jsonInt(d->second.meta[ZT_SOFTWARE_UPDATE_JSON_VENDOR],0) == rvVendor)&&
								    (OSUtils::jsonString(d->second.meta[ZT_SOFTWARE_UPDATE_JSON_CHANNEL],"") == rvChannel)&&
								    (OSUtils::jsonString(d->second.meta[ZT_SOFTWARE_UPDATE_JSON_UPDATE_SIGNED_BY],"") == expectedSigner)) {
									const unsigned int dvMaj = (unsigned int)OSUtils::jsonInt(d->second.meta[ZT_SOFTWARE_UPDATE_JSON_VERSION_MAJOR],0);
									const unsigned int dvMin = (unsigned int)OSUtils::jsonInt(d->second.meta[ZT_SOFTWARE_UPDATE_JSON_VERSION_MINOR],0);
									const unsigned int dvRev = (unsigned int)OSUtils::jsonInt(d->second.meta[ZT_SOFTWARE_UPDATE_JSON_VERSION_REVISION],0);
									const unsigned int dvBld = (unsigned int)OSUtils::jsonInt(d->second.meta[ZT_SOFTWARE_UPDATE_JSON_VERSION_BUILD],0);
									if (Utils::compareVersion(dvMaj,dvMin,dvRev,dvBld,bestVMaj,bestVMin,bestVRev,bestVBld) > 0) {
										latest = &(d->second.meta);
										bestVMaj = dvMaj;
										bestVMin = dvMin;
										bestVRev = dvRev;
										bestVBld = dvBld;
									}
								}
							}
							if (latest) {
								std::string lj;
								lj.push_back((char)VERB_LATEST);
								lj.append(OSUtils::jsonDump(*latest));
								_node.sendUserMessage(origin,ZT_SOFTWARE_UPDATE_USER_MESSAGE_TYPE,lj.data(),(unsigned int)lj.length());
								if (_distLog) {
									fprintf(_distLog,"%.10llx GET_LATEST %u.%u.%u_%u platform %u arch %u vendor %u channel %s -> LATEST %u.%u.%u_%u" ZT_EOL_S,(unsigned long long)origin,rvMaj,rvMin,rvRev,rvBld,rvPlatform,rvArch,rvVendor,rvChannel.c_str(),bestVMaj,bestVMin,bestVRev,bestVBld);
									fflush(_distLog);
								}
							}
						} // else no reply, since we have nothing to distribute

					} else { // VERB_LATEST

						if ((origin == ZT_SOFTWARE_UPDATE_SERVICE)&&
							  (Utils::compareVersion(rvMaj,rvMin,rvRev,rvBld,ZEROTIER_ONE_VERSION_MAJOR,ZEROTIER_ONE_VERSION_MINOR,ZEROTIER_ONE_VERSION_REVISION,ZEROTIER_ONE_VERSION_BUILD) > 0)&&
							  (OSUtils::jsonString(req[ZT_SOFTWARE_UPDATE_JSON_UPDATE_SIGNED_BY],"") == ZT_SOFTWARE_UPDATE_SIGNING_AUTHORITY)) {
							const unsigned long len = (unsigned long)OSUtils::jsonInt(req[ZT_SOFTWARE_UPDATE_JSON_UPDATE_SIZE],0);
							const std::string hash = OSUtils::jsonBinFromHex(req[ZT_SOFTWARE_UPDATE_JSON_UPDATE_HASH]);
							if ((len <= ZT_SOFTWARE_UPDATE_MAX_SIZE)&&(hash.length() >= 16)) {
								if (_latestMeta != req) {
									_latestMeta = req;
									_latestValid = false;

									OSUtils::rm((_homePath + ZT_PATH_SEPARATOR_S ZT_SOFTWARE_UPDATE_META_FILENAME).c_str());
									OSUtils::rm((_homePath + ZT_PATH_SEPARATOR_S ZT_SOFTWARE_UPDATE_BIN_FILENAME).c_str());

									_download = std::string();
									memcpy(_downloadHashPrefix.data,hash.data(),16);
									_downloadLength = len;
								}

								if ((_downloadLength > 0)&&(_download.length() < _downloadLength)) {
									Buffer<128> gd;
									gd.append((uint8_t)VERB_GET_DATA);
									gd.append(_downloadHashPrefix.data,16);
									gd.append((uint32_t)_download.length());
									_node.sendUserMessage(ZT_SOFTWARE_UPDATE_SERVICE,ZT_SOFTWARE_UPDATE_USER_MESSAGE_TYPE,gd.data(),gd.size());
									//printf(">> GET_DATA @%u\n",(unsigned int)_download.length());
								}
							}
						}
					}

				}
			}	break;

			case VERB_GET_DATA:
				if ((len >= 21)&&(_dist.size() > 0)) {
					unsigned long idx = (unsigned long)*(reinterpret_cast<const uint8_t *>(data) + 17) << 24;
					idx |= (unsigned long)*(reinterpret_cast<const uint8_t *>(data) + 18) << 16;
					idx |= (unsigned long)*(reinterpret_cast<const uint8_t *>(data) + 19) << 8;
					idx |= (unsigned long)*(reinterpret_cast<const uint8_t *>(data) + 20);
					//printf("<< GET_DATA @%u from %.10llx for %s\n",(unsigned int)idx,origin,Utils::hex(reinterpret_cast<const uint8_t *>(data) + 1,16).c_str());
					std::map< Array<uint8_t,16>,_D >::iterator d(_dist.find(Array<uint8_t,16>(reinterpret_cast<const uint8_t *>(data) + 1)));
					if ((d != _dist.end())&&(idx < (unsigned long)d->second.bin.length())) {
						Buffer<ZT_SOFTWARE_UPDATE_CHUNK_SIZE + 128> buf;
						buf.append((uint8_t)VERB_DATA);
						buf.append(reinterpret_cast<const uint8_t *>(data) + 1,16);
						buf.append((uint32_t)idx);
						buf.append(d->second.bin.data() + idx,std::min((unsigned long)ZT_SOFTWARE_UPDATE_CHUNK_SIZE,(unsigned long)(d->second.bin.length() - idx)));
						_node.sendUserMessage(origin,ZT_SOFTWARE_UPDATE_USER_MESSAGE_TYPE,buf.data(),buf.size());
						//printf(">> DATA @%u\n",(unsigned int)idx);
					}
				}
				break;

			case VERB_DATA:
				if ((len >= 21)&&(_downloadLength > 0)&&(!memcmp(_downloadHashPrefix.data,reinterpret_cast<const uint8_t *>(data) + 1,16))) {
					unsigned long idx = (unsigned long)*(reinterpret_cast<const uint8_t *>(data) + 17) << 24;
					idx |= (unsigned long)*(reinterpret_cast<const uint8_t *>(data) + 18) << 16;
					idx |= (unsigned long)*(reinterpret_cast<const uint8_t *>(data) + 19) << 8;
					idx |= (unsigned long)*(reinterpret_cast<const uint8_t *>(data) + 20);
					//printf("<< DATA @%u / %u bytes (we now have %u bytes)\n",(unsigned int)idx,(unsigned int)(len - 21),(unsigned int)_download.length());
					if (idx == (unsigned long)_download.length()) {
						_download.append(reinterpret_cast<const char *>(data) + 21,len - 21);
						if (_download.length() < _downloadLength) {
							Buffer<128> gd;
							gd.append((uint8_t)VERB_GET_DATA);
							gd.append(_downloadHashPrefix.data,16);
							gd.append((uint32_t)_download.length());
							_node.sendUserMessage(ZT_SOFTWARE_UPDATE_SERVICE,ZT_SOFTWARE_UPDATE_USER_MESSAGE_TYPE,gd.data(),gd.size());
							//printf(">> GET_DATA @%u\n",(unsigned int)_download.length());
						}
					}
				}
				break;

			default:
				if (_distLog) {
					fprintf(_distLog,"%.10llx WARNING: bad update message verb==%u length==%u (unrecognized verb)" ZT_EOL_S,(unsigned long long)origin,(unsigned int)v,len);
					fflush(_distLog);
				}
				break;
		}
	} catch ( ... ) {
		if (_distLog) {
			fprintf(_distLog,"%.10llx WARNING: bad update message verb==%u length==%u (unexpected exception, likely invalid JSON)" ZT_EOL_S,(unsigned long long)origin,(unsigned int)v,len);
			fflush(_distLog);
		}
	}
}

bool SoftwareUpdater::check(const uint64_t now)
{
	if ((now - _lastCheckTime) >= ZT_SOFTWARE_UPDATE_CHECK_PERIOD) {
		_lastCheckTime = now;
		char tmp[512];
		const unsigned int len = Utils::snprintf(tmp,sizeof(tmp),
			"%c{\"" ZT_SOFTWARE_UPDATE_JSON_VERSION_MAJOR "\":%d,"
			"\"" ZT_SOFTWARE_UPDATE_JSON_VERSION_MINOR "\":%d,"
			"\"" ZT_SOFTWARE_UPDATE_JSON_VERSION_REVISION "\":%d,"
			"\"" ZT_SOFTWARE_UPDATE_JSON_VERSION_BUILD "\":%d,"
			"\"" ZT_SOFTWARE_UPDATE_JSON_EXPECT_SIGNED_BY "\":\"%s\","
			"\"" ZT_SOFTWARE_UPDATE_JSON_PLATFORM "\":%d,"
			"\"" ZT_SOFTWARE_UPDATE_JSON_ARCHITECTURE "\":%d,"
			"\"" ZT_SOFTWARE_UPDATE_JSON_VENDOR "\":%d,"
			"\"" ZT_SOFTWARE_UPDATE_JSON_CHANNEL "\":\"%s\"}",
			(char)VERB_GET_LATEST,
			ZEROTIER_ONE_VERSION_MAJOR,
			ZEROTIER_ONE_VERSION_MINOR,
			ZEROTIER_ONE_VERSION_REVISION,
			ZEROTIER_ONE_VERSION_BUILD,
			ZT_SOFTWARE_UPDATE_SIGNING_AUTHORITY,
			ZT_BUILD_PLATFORM,
			ZT_BUILD_ARCHITECTURE,
			(int)ZT_VENDOR_ZEROTIER,
			_channel.c_str());
		_node.sendUserMessage(ZT_SOFTWARE_UPDATE_SERVICE,ZT_SOFTWARE_UPDATE_USER_MESSAGE_TYPE,tmp,len);
		//printf(">> GET_LATEST\n");
	}

	if (_latestValid)
		return true;

	if (_downloadLength > 0) {
		if (_download.length() >= _downloadLength) {
			// This is the very important security validation part that makes sure
			// this software update doesn't have cooties.

			const std::string metaPath(_homePath + ZT_PATH_SEPARATOR_S ZT_SOFTWARE_UPDATE_META_FILENAME);
			const std::string binPath(_homePath + ZT_PATH_SEPARATOR_S ZT_SOFTWARE_UPDATE_BIN_FILENAME);

			try {
				// (1) Check the hash itself to make sure the image is basically okay
				uint8_t sha512[ZT_SHA512_DIGEST_LEN];
				SHA512::hash(sha512,_download.data(),(unsigned int)_download.length());
				if (Utils::hex(sha512,ZT_SHA512_DIGEST_LEN) == OSUtils::jsonString(_latestMeta[ZT_SOFTWARE_UPDATE_JSON_UPDATE_HASH],"")) {
					// (2) Check signature by signing authority
					const std::string sig(OSUtils::jsonBinFromHex(_latestMeta[ZT_SOFTWARE_UPDATE_JSON_UPDATE_SIGNATURE]));
					if (Identity(ZT_SOFTWARE_UPDATE_SIGNING_AUTHORITY).verify(_download.data(),(unsigned int)_download.length(),sig.data(),(unsigned int)sig.length())) {
						// (3) Try to save file, and if so we are good.
						if (OSUtils::writeFile(metaPath.c_str(),OSUtils::jsonDump(_latestMeta)) && OSUtils::writeFile(binPath.c_str(),_download)) {
							OSUtils::lockDownFile(metaPath.c_str(),false);
							OSUtils::lockDownFile(binPath.c_str(),false);
							_latestValid = true;
							//printf("VALID UPDATE\n%s\n",OSUtils::jsonDump(_latestMeta).c_str());
							_download = std::string();
							_downloadLength = 0;
							return true;
						}
					}
				}
			} catch ( ... ) {} // any exception equals verification failure

			// If we get here, checks failed.
			//printf("INVALID UPDATE (!!!)\n%s\n",OSUtils::jsonDump(_latestMeta).c_str());
			OSUtils::rm(metaPath.c_str());
			OSUtils::rm(binPath.c_str());
			_latestMeta = nlohmann::json();
			_latestValid = false;
			_download = std::string();
			_downloadLength = 0;
		} else {
			Buffer<128> gd;
			gd.append((uint8_t)VERB_GET_DATA);
			gd.append(_downloadHashPrefix.data,16);
			gd.append((uint32_t)_download.length());
			_node.sendUserMessage(ZT_SOFTWARE_UPDATE_SERVICE,ZT_SOFTWARE_UPDATE_USER_MESSAGE_TYPE,gd.data(),gd.size());
			//printf(">> GET_DATA @%u\n",(unsigned int)_download.length());
		}
	}

	return false;
}

void SoftwareUpdater::apply()
{
	std::string updatePath(_homePath + ZT_PATH_SEPARATOR_S ZT_SOFTWARE_UPDATE_BIN_FILENAME);
	if ((_latestMeta.is_object())&&(_latestValid)&&(OSUtils::fileExists(updatePath.c_str(),false))) {
#ifdef __WINDOWS__
		std::string cmdArgs(OSUtils::jsonString(_latestMeta[ZT_SOFTWARE_UPDATE_JSON_UPDATE_EXEC_ARGS],""));
		if (cmdArgs.length() > 0) {
			updatePath.push_back(' ');
			updatePath.append(cmdArgs);
		}
		STARTUPINFOA si;
		PROCESS_INFORMATION pi;
		memset(&si,0,sizeof(si));
		memset(&pi,0,sizeof(pi));
		CreateProcessA(NULL,const_cast<LPSTR>(updatePath.c_str()),NULL,NULL,FALSE,CREATE_NO_WINDOW|CREATE_NEW_PROCESS_GROUP,NULL,NULL,&si,&pi);
		// Windows doesn't exit here -- updater will stop the service during update, etc. -- but we do want to stop multiple runs from happening
		_latestMeta = nlohmann::json();
		_latestValid = false;
#else
		char *argv[256];
		unsigned long ac = 0;
		argv[ac++] = const_cast<char *>(updatePath.c_str());
		const std::vector<std::string> argsSplit(OSUtils::split(OSUtils::jsonString(_latestMeta[ZT_SOFTWARE_UPDATE_JSON_UPDATE_EXEC_ARGS],"").c_str()," ","\\","\""));
		for(std::vector<std::string>::const_iterator a(argsSplit.begin());a!=argsSplit.end();++a) {
			argv[ac] = const_cast<char *>(a->c_str());
			if (++ac == 255) break;
		}
		argv[ac] = (char *)0;
		chmod(updatePath.c_str(),0700);

		// Close all open file descriptors except stdout/stderr/etc.
		int minMyFd = STDIN_FILENO;
		if (STDOUT_FILENO > minMyFd) minMyFd = STDOUT_FILENO;
		if (STDERR_FILENO > minMyFd) minMyFd = STDERR_FILENO;
		++minMyFd;
#ifdef _SC_OPEN_MAX
		int maxMyFd = (int)sysconf(_SC_OPEN_MAX);
		if (maxMyFd <= minMyFd)
			maxMyFd = 65536;
#else
		int maxMyFd = 65536;
#endif
		while (minMyFd < maxMyFd)
			close(minMyFd++);

		execv(updatePath.c_str(),argv);
		fprintf(stderr,"FATAL: unable to execute software update binary at %s\n",updatePath.c_str());
		exit(1);
#endif
	}
}

} // namespace ZeroTier
