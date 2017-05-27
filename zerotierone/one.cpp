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
#include <time.h>
#include <errno.h>

#include "node/Constants.hpp"

#ifdef __WINDOWS__
#include <WinSock2.h>
#include <Windows.h>
#include <tchar.h>
#include <wchar.h>
#include <lmcons.h>
#include <newdev.h>
#include <atlbase.h>
#include "osdep/WindowsEthernetTap.hpp"
#include "windows/ZeroTierOne/ServiceInstaller.h"
#include "windows/ZeroTierOne/ServiceBase.h"
#include "windows/ZeroTierOne/ZeroTierOneService.h"
#else
#include <unistd.h>
#include <pwd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <dirent.h>
#include <signal.h>
#ifdef __LINUX__
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <linux/capability.h>
#include <linux/securebits.h>
#endif
#endif

#include <string>
#include <stdexcept>
#include <iostream>
#include <sstream>

#include "version.h"
#include "include/ZeroTierOne.h"

#include "node/Identity.hpp"
#include "node/CertificateOfMembership.hpp"
#include "node/Utils.hpp"
#include "node/NetworkController.hpp"
#include "node/Buffer.hpp"
#include "node/World.hpp"

#include "osdep/OSUtils.hpp"
#include "osdep/Http.hpp"

#include "service/OneService.hpp"

#include "ext/json/json.hpp"

#define ZT_PID_PATH "zerotier-one.pid"

using namespace ZeroTier;

static OneService *volatile zt1Service = (OneService *)0;

#define PROGRAM_NAME "ZeroTier One"
#define COPYRIGHT_NOTICE "Copyright © 2011–2016 ZeroTier, Inc."
#define LICENSE_GRANT \
	"This is free software: you may copy, modify, and/or distribute this" ZT_EOL_S \
	"work under the terms of the GNU General Public License, version 3 or" ZT_EOL_S \
	"later as published by the Free Software Foundation." ZT_EOL_S \
	"No warranty expressed or implied." ZT_EOL_S

/****************************************************************************/
/* zerotier-cli personality                                                 */
/****************************************************************************/

// This is getting deprecated soon in favor of the stuff in cli/

static void cliPrintHelp(const char *pn,FILE *out)
{
	fprintf(out,
		"%s version %d.%d.%d" ZT_EOL_S,
		PROGRAM_NAME,
		ZEROTIER_ONE_VERSION_MAJOR, ZEROTIER_ONE_VERSION_MINOR, ZEROTIER_ONE_VERSION_REVISION);
	fprintf(out,
		COPYRIGHT_NOTICE ZT_EOL_S
		LICENSE_GRANT ZT_EOL_S);
	fprintf(out,"Usage: %s [-switches] <command/path> [<args>]" ZT_EOL_S"" ZT_EOL_S,pn);
	fprintf(out,"Available switches:" ZT_EOL_S);
	fprintf(out,"  -h                      - Display this help" ZT_EOL_S);
	fprintf(out,"  -v                      - Show version" ZT_EOL_S);
	fprintf(out,"  -j                      - Display full raw JSON output" ZT_EOL_S);
	fprintf(out,"  -D<path>                - ZeroTier home path for parameter auto-detect" ZT_EOL_S);
	fprintf(out,"  -p<port>                - HTTP port (default: auto)" ZT_EOL_S);
	fprintf(out,"  -T<token>               - Authentication token (default: auto)" ZT_EOL_S);
	fprintf(out,ZT_EOL_S"Available commands:" ZT_EOL_S);
	fprintf(out,"  info                    - Display status info" ZT_EOL_S);
	fprintf(out,"  listpeers               - List all peers" ZT_EOL_S);
	fprintf(out,"  listnetworks            - List all networks" ZT_EOL_S);
	fprintf(out,"  join <network>          - Join a network" ZT_EOL_S);
	fprintf(out,"  leave <network>         - Leave a network" ZT_EOL_S);
	fprintf(out,"  set <network> <setting> - Set a network setting" ZT_EOL_S);
	fprintf(out,"  listmoons               - List moons (federated root sets)" ZT_EOL_S);
	fprintf(out,"  orbit <world ID> <seed> - Join a moon via any member root" ZT_EOL_S);
	fprintf(out,"  deorbit <world ID>      - Leave a moon" ZT_EOL_S);
}

static std::string cliFixJsonCRs(const std::string &s)
{
	std::string r;
	for(std::string::const_iterator c(s.begin());c!=s.end();++c) {
		if (*c == '\n')
			r.append(ZT_EOL_S);
		else r.push_back(*c);
	}
	return r;
}

#ifdef __WINDOWS__
static int cli(int argc, _TCHAR* argv[])
#else
static int cli(int argc,char **argv)
#endif
{
	unsigned int port = 0;
	std::string homeDir,command,arg1,arg2,authToken;
	std::string ip("127.0.0.1");
	bool json = false;
	for(int i=1;i<argc;++i) {
		if (argv[i][0] == '-') {
			switch(argv[i][1]) {

				case 'q': // ignore -q used to invoke this personality
					if (argv[i][2]) {
						cliPrintHelp(argv[0],stdout);
						return 1;
					}
					break;

				case 'j':
					if (argv[i][2]) {
						cliPrintHelp(argv[0],stdout);
						return 1;
					}
					json = true;
					break;

				case 'p':
					port = Utils::strToUInt(argv[i] + 2);
					if ((port > 0xffff)||(port == 0)) {
						cliPrintHelp(argv[0],stdout);
						return 1;
					}
					break;

				case 'D':
					if (argv[i][2]) {
						homeDir = argv[i] + 2;
					} else {
						cliPrintHelp(argv[0],stdout);
						return 1;
					}
					break;

				case 'H':
					if (argv[i][2]) {
						ip = argv[i] + 2;
					} else {
						cliPrintHelp(argv[0],stdout);
						return 1;
					}
					break;

				case 'T':
					if (argv[i][2]) {
						authToken = argv[i] + 2;
					} else {
						cliPrintHelp(argv[0],stdout);
						return 1;
					}
					break;

				case 'v':
					if (argv[i][2]) {
						cliPrintHelp(argv[0],stdout);
						return 1;
					}
					printf("%d.%d.%d" ZT_EOL_S,ZEROTIER_ONE_VERSION_MAJOR,ZEROTIER_ONE_VERSION_MINOR,ZEROTIER_ONE_VERSION_REVISION);
					return 0;

				case 'h':
				case '?':
				default:
					cliPrintHelp(argv[0],stdout);
					return 0;
			}
		} else {
			if (arg1.length())
				arg2 = argv[i];
			else if (command.length())
				arg1 = argv[i];
			else command = argv[i];
		}
	}
	if (!homeDir.length())
		homeDir = OneService::platformDefaultHomePath();

	if ((!port)||(!authToken.length())) {
		if (!homeDir.length()) {
			fprintf(stderr,"%s: missing port or authentication token and no home directory specified to auto-detect" ZT_EOL_S,argv[0]);
			return 2;
		}

		if (!port) {
			std::string portStr;
			OSUtils::readFile((homeDir + ZT_PATH_SEPARATOR_S + "zerotier-one.port").c_str(),portStr);
			port = Utils::strToUInt(portStr.c_str());
			if ((port == 0)||(port > 0xffff)) {
				fprintf(stderr,"%s: missing port and zerotier-one.port not found in %s" ZT_EOL_S,argv[0],homeDir.c_str());
				return 2;
			}
		}

		if (!authToken.length()) {
			OSUtils::readFile((homeDir + ZT_PATH_SEPARATOR_S + "authtoken.secret").c_str(),authToken);
#ifdef __UNIX_LIKE__
			if (!authToken.length()) {
				const char *hd = getenv("HOME");
				if (hd) {
					char p[4096];
#ifdef __APPLE__
					Utils::snprintf(p,sizeof(p),"%s/Library/Application Support/ZeroTier/One/authtoken.secret",hd);
#else
					Utils::snprintf(p,sizeof(p),"%s/.zeroTierOneAuthToken",hd);
#endif
					OSUtils::readFile(p,authToken);
				}
			}
#endif
			if (!authToken.length()) {
				fprintf(stderr,"%s: missing authentication token and authtoken.secret not found (or readable) in %s" ZT_EOL_S,argv[0],homeDir.c_str());
				return 2;
			}
		}
	}

	InetAddress addr;
	{
		char addrtmp[256];
		Utils::snprintf(addrtmp,sizeof(addrtmp),"%s/%u",ip.c_str(),port);
		addr = InetAddress(addrtmp);
	}

	std::map<std::string,std::string> requestHeaders;
	std::map<std::string,std::string> responseHeaders;
	std::string responseBody;

	requestHeaders["X-ZT1-Auth"] = authToken;

	if ((command.length() > 0)&&(command[0] == '/')) {
		unsigned int scode = Http::GET(
			1024 * 1024 * 16,
			60000,
			(const struct sockaddr *)&addr,
			command.c_str(),
			requestHeaders,
			responseHeaders,
			responseBody);
		if (scode == 200) {
			printf("%s",cliFixJsonCRs(responseBody).c_str());
			return 0;
		} else {
			printf("%u %s %s" ZT_EOL_S,scode,command.c_str(),responseBody.c_str());
			return 1;
		}
	} else if ((command == "info")||(command == "status")) {
		const unsigned int scode = Http::GET(1024 * 1024 * 16,60000,(const struct sockaddr *)&addr,"/status",requestHeaders,responseHeaders,responseBody);

		nlohmann::json j;
		try {
			j = OSUtils::jsonParse(responseBody);
		} catch (std::exception &exc) {
			printf("%u %s invalid JSON response (%s)" ZT_EOL_S,scode,command.c_str(),exc.what());
			return 1;
		} catch ( ... ) {
			printf("%u %s invalid JSON response (unknown exception)" ZT_EOL_S,scode,command.c_str());
			return 1;
		}

		if (scode == 200) {
			if (json) {
				printf("%s" ZT_EOL_S,OSUtils::jsonDump(j).c_str());
			} else {
				if (j.is_object()) {
					printf("200 info %s %s %s" ZT_EOL_S,
						OSUtils::jsonString(j["address"],"-").c_str(),
						OSUtils::jsonString(j["version"],"-").c_str(),
						((j["tcpFallbackActive"]) ? "TUNNELED" : ((j["online"]) ? "ONLINE" : "OFFLINE")));
				}
			}
			return 0;
		} else {
			printf("%u %s %s" ZT_EOL_S,scode,command.c_str(),responseBody.c_str());
			return 1;
		}
	} else if (command == "listpeers") {
		const unsigned int scode = Http::GET(1024 * 1024 * 16,60000,(const struct sockaddr *)&addr,"/peer",requestHeaders,responseHeaders,responseBody);

		nlohmann::json j;
		try {
			j = OSUtils::jsonParse(responseBody);
		} catch (std::exception &exc) {
			printf("%u %s invalid JSON response (%s)" ZT_EOL_S,scode,command.c_str(),exc.what());
			return 1;
		} catch ( ... ) {
			printf("%u %s invalid JSON response (unknown exception)" ZT_EOL_S,scode,command.c_str());
			return 1;
		}

		if (scode == 200) {
			if (json) {
				printf("%s" ZT_EOL_S,OSUtils::jsonDump(j).c_str());
			} else {
				printf("200 listpeers <ztaddr> <path> <latency> <version> <role>" ZT_EOL_S);
				if (j.is_array()) {
					for(unsigned long k=0;k<j.size();++k) {
						nlohmann::json &p = j[k];
						std::string bestPath;
						nlohmann::json &paths = p["paths"];
						if (paths.is_array()) {
							for(unsigned long i=0;i<paths.size();++i) {
								nlohmann::json &path = paths[i];
								if (path["preferred"]) {
									char tmp[256];
									std::string addr = path["address"];
									const uint64_t now = OSUtils::now();
									const double lq = (path.count("linkQuality")) ? (double)path["linkQuality"] : -1.0;
									Utils::snprintf(tmp,sizeof(tmp),"%s;%llu;%llu;%1.2f",addr.c_str(),now - (uint64_t)path["lastSend"],now - (uint64_t)path["lastReceive"],lq);
									bestPath = tmp;
									break;
								}
							}
						}
						if (bestPath.length() == 0) bestPath = "-";
						char ver[128];
						int64_t vmaj = p["versionMajor"];
						int64_t vmin = p["versionMinor"];
						int64_t vrev = p["versionRev"];
						if (vmaj >= 0) {
							Utils::snprintf(ver,sizeof(ver),"%lld.%lld.%lld",vmaj,vmin,vrev);
						} else {
							ver[0] = '-';
							ver[1] = (char)0;
						}
						printf("200 listpeers %s %s %d %s %s" ZT_EOL_S,
							OSUtils::jsonString(p["address"],"-").c_str(),
							bestPath.c_str(),
							(int)OSUtils::jsonInt(p["latency"],0),
							ver,
							OSUtils::jsonString(p["role"],"-").c_str());
					}
				}
			}
			return 0;
		} else {
			printf("%u %s %s" ZT_EOL_S,scode,command.c_str(),responseBody.c_str());
			return 1;
		}
	} else if (command == "listnetworks") {
		const unsigned int scode = Http::GET(1024 * 1024 * 16,60000,(const struct sockaddr *)&addr,"/network",requestHeaders,responseHeaders,responseBody);

		nlohmann::json j;
		try {
			j = OSUtils::jsonParse(responseBody);
		} catch (std::exception &exc) {
			printf("%u %s invalid JSON response (%s)" ZT_EOL_S,scode,command.c_str(),exc.what());
			return 1;
		} catch ( ... ) {
			printf("%u %s invalid JSON response (unknown exception)" ZT_EOL_S,scode,command.c_str());
			return 1;
		}

		if (scode == 200) {
			if (json) {
				printf("%s" ZT_EOL_S,OSUtils::jsonDump(j).c_str());
			} else {
				printf("200 listnetworks <nwid> <name> <mac> <status> <type> <dev> <ZT assigned ips>" ZT_EOL_S);
				if (j.is_array()) {
					for(unsigned long i=0;i<j.size();++i) {
						nlohmann::json &n = j[i];
						if (n.is_object()) {
							std::string aa;
							nlohmann::json &assignedAddresses = n["assignedAddresses"];
							if (assignedAddresses.is_array()) {
								for(unsigned long j=0;j<assignedAddresses.size();++j) {
									nlohmann::json &addr = assignedAddresses[j];
									if (addr.is_string()) {
										if (aa.length() > 0) aa.push_back(',');
										aa.append(addr.get<std::string>());
									}
								}
							}
							if (aa.length() == 0) aa = "-";
							printf("200 listnetworks %s %s %s %s %s %s %s" ZT_EOL_S,
								OSUtils::jsonString(n["nwid"],"-").c_str(),
								OSUtils::jsonString(n["name"],"-").c_str(),
								OSUtils::jsonString(n["mac"],"-").c_str(),
								OSUtils::jsonString(n["status"],"-").c_str(),
								OSUtils::jsonString(n["type"],"-").c_str(),
								OSUtils::jsonString(n["portDeviceName"],"-").c_str(),
								aa.c_str());
						}
					}
				}
			}
			return 0;
		} else {
			printf("%u %s %s" ZT_EOL_S,scode,command.c_str(),responseBody.c_str());
			return 1;
		}
	} else if (command == "join") {
		if (arg1.length() != 16) {
			cliPrintHelp(argv[0],stderr);
			return 2;
		}
		requestHeaders["Content-Type"] = "application/json";
		requestHeaders["Content-Length"] = "2";
		unsigned int scode = Http::POST(
			1024 * 1024 * 16,
			60000,
			(const struct sockaddr *)&addr,
			(std::string("/network/") + arg1).c_str(),
			requestHeaders,
			"{}",
			2,
			responseHeaders,
			responseBody);
		if (scode == 200) {
			if (json) {
				printf("%s",cliFixJsonCRs(responseBody).c_str());
			} else {
				printf("200 join OK" ZT_EOL_S);
			}
			return 0;
		} else {
			printf("%u %s %s" ZT_EOL_S,scode,command.c_str(),responseBody.c_str());
			return 1;
		}
	} else if (command == "leave") {
		if (arg1.length() != 16) {
			cliPrintHelp(argv[0],stderr);
			return 2;
		}
		unsigned int scode = Http::DEL(
			1024 * 1024 * 16,
			60000,
			(const struct sockaddr *)&addr,
			(std::string("/network/") + arg1).c_str(),
			requestHeaders,
			responseHeaders,
			responseBody);
		if (scode == 200) {
			if (json) {
				printf("%s",cliFixJsonCRs(responseBody).c_str());
			} else {
				printf("200 leave OK" ZT_EOL_S);
			}
			return 0;
		} else {
			printf("%u %s %s" ZT_EOL_S,scode,command.c_str(),responseBody.c_str());
			return 1;
		}
	} else if (command == "listmoons") {
		const unsigned int scode = Http::GET(1024 * 1024 * 16,60000,(const struct sockaddr *)&addr,"/moon",requestHeaders,responseHeaders,responseBody);

		nlohmann::json j;
		try {
			j = OSUtils::jsonParse(responseBody);
		} catch (std::exception &exc) {
			printf("%u %s invalid JSON response (%s)" ZT_EOL_S,scode,command.c_str(),exc.what());
			return 1;
		} catch ( ... ) {
			printf("%u %s invalid JSON response (unknown exception)" ZT_EOL_S,scode,command.c_str());
			return 1;
		}

		if (scode == 200) {
			printf("%s" ZT_EOL_S,OSUtils::jsonDump(j).c_str());
			return 0;
		} else {
			printf("%u %s %s" ZT_EOL_S,scode,command.c_str(),responseBody.c_str());
			return 1;
		}
	} else if (command == "orbit") {
		const uint64_t worldId = Utils::hexStrToU64(arg1.c_str());
		const uint64_t seed = Utils::hexStrToU64(arg2.c_str());
		if ((worldId)&&(seed)) {
			char jsons[1024];
			Utils::snprintf(jsons,sizeof(jsons),"{\"seed\":\"%s\"}",arg2.c_str());
			char cl[128];
			Utils::snprintf(cl,sizeof(cl),"%u",(unsigned int)strlen(jsons));
			requestHeaders["Content-Type"] = "application/json";
			requestHeaders["Content-Length"] = cl;
			unsigned int scode = Http::POST(
				1024 * 1024 * 16,
				60000,
				(const struct sockaddr *)&addr,
				(std::string("/moon/") + arg1).c_str(),
				requestHeaders,
				jsons,
				(unsigned long)strlen(jsons),
				responseHeaders,
				responseBody);
			if (scode == 200) {
				printf("200 orbit OK" ZT_EOL_S);
				return 0;
			} else {
				printf("%u %s %s" ZT_EOL_S,scode,command.c_str(),responseBody.c_str());
				return 1;
			}
		}
	} else if (command == "deorbit") {
		unsigned int scode = Http::DEL(
			1024 * 1024 * 16,
			60000,
			(const struct sockaddr *)&addr,
			(std::string("/moon/") + arg1).c_str(),
			requestHeaders,
			responseHeaders,
			responseBody);
		if (scode == 200) {
			if (json) {
				printf("%s",cliFixJsonCRs(responseBody).c_str());
			} else {
				printf("200 deorbit OK" ZT_EOL_S);
			}
			return 0;
		} else {
			printf("%u %s %s" ZT_EOL_S,scode,command.c_str(),responseBody.c_str());
			return 1;
		}
	} else if (command == "set") {
		if (arg1.length() != 16) {
			cliPrintHelp(argv[0],stderr);
			return 2;
		}
		std::size_t eqidx = arg2.find('=');
		if (eqidx != std::string::npos) {
			if ((arg2.substr(0,eqidx) == "allowManaged")||(arg2.substr(0,eqidx) == "allowGlobal")||(arg2.substr(0,eqidx) == "allowDefault")) {
				char jsons[1024];
				Utils::snprintf(jsons,sizeof(jsons),"{\"%s\":%s}",
					arg2.substr(0,eqidx).c_str(),
					(((arg2.substr(eqidx,2) == "=t")||(arg2.substr(eqidx,2) == "=1")) ? "true" : "false"));
				char cl[128];
				Utils::snprintf(cl,sizeof(cl),"%u",(unsigned int)strlen(jsons));
				requestHeaders["Content-Type"] = "application/json";
				requestHeaders["Content-Length"] = cl;
				unsigned int scode = Http::POST(
					1024 * 1024 * 16,
					60000,
					(const struct sockaddr *)&addr,
					(std::string("/network/") + arg1).c_str(),
					requestHeaders,
					jsons,
					(unsigned long)strlen(jsons),
					responseHeaders,
					responseBody);
				if (scode == 200) {
					printf("%s",cliFixJsonCRs(responseBody).c_str());
					return 0;
				} else {
					printf("%u %s %s" ZT_EOL_S,scode,command.c_str(),responseBody.c_str());
					return 1;
				}
			}
		} else {
			cliPrintHelp(argv[0],stderr);
			return 2;
		}
	} else {
		cliPrintHelp(argv[0],stderr);
		return 0;
	}

	return 0;
}

/****************************************************************************/
/* zerotier-idtool personality                                              */
/****************************************************************************/

static void idtoolPrintHelp(FILE *out,const char *pn)
{
	fprintf(out,
		"%s version %d.%d.%d" ZT_EOL_S,
		PROGRAM_NAME,
		ZEROTIER_ONE_VERSION_MAJOR, ZEROTIER_ONE_VERSION_MINOR, ZEROTIER_ONE_VERSION_REVISION);
	fprintf(out,
		COPYRIGHT_NOTICE ZT_EOL_S
		LICENSE_GRANT ZT_EOL_S);
	fprintf(out,"Usage: %s <command> [<args>]" ZT_EOL_S"" ZT_EOL_S"Commands:" ZT_EOL_S,pn);
	fprintf(out,"  generate [<identity.secret>] [<identity.public>] [<vanity>]" ZT_EOL_S);
	fprintf(out,"  validate <identity.secret/public>" ZT_EOL_S);
	fprintf(out,"  getpublic <identity.secret>" ZT_EOL_S);
	fprintf(out,"  sign <identity.secret> <file>" ZT_EOL_S);
	fprintf(out,"  verify <identity.secret/public> <file> <signature>" ZT_EOL_S);
	fprintf(out,"  initmoon <identity.public of first seed>" ZT_EOL_S);
	fprintf(out,"  genmoon <moon json>" ZT_EOL_S);
}

static Identity getIdFromArg(char *arg)
{
	Identity id;
	if ((strlen(arg) > 32)&&(arg[10] == ':')) { // identity is a literal on the command line
		if (id.fromString(arg))
			return id;
	} else { // identity is to be read from a file
		std::string idser;
		if (OSUtils::readFile(arg,idser)) {
			if (id.fromString(idser))
				return id;
		}
	}
	return Identity();
}

#ifdef __WINDOWS__
static int idtool(int argc, _TCHAR* argv[])
#else
static int idtool(int argc,char **argv)
#endif
{
	if (argc < 2) {
		idtoolPrintHelp(stdout,argv[0]);
		return 1;
	}

	if (!strcmp(argv[1],"generate")) {
		uint64_t vanity = 0;
		int vanityBits = 0;
		if (argc >= 5) {
			vanity = Utils::hexStrToU64(argv[4]) & 0xffffffffffULL;
			vanityBits = 4 * (int)strlen(argv[4]);
			if (vanityBits > 40)
				vanityBits = 40;
		}

		Identity id;
		for(;;) {
			id.generate();
			if ((id.address().toInt() >> (40 - vanityBits)) == vanity) {
				if (vanityBits > 0) {
					fprintf(stderr,"vanity address: found %.10llx !\n",(unsigned long long)id.address().toInt());
				}
				break;
			} else {
				fprintf(stderr,"vanity address: tried %.10llx looking for first %d bits of %.10llx\n",(unsigned long long)id.address().toInt(),vanityBits,(unsigned long long)(vanity << (40 - vanityBits)));
			}
		}

		std::string idser = id.toString(true);
		if (argc >= 3) {
			if (!OSUtils::writeFile(argv[2],idser)) {
				fprintf(stderr,"Error writing to %s" ZT_EOL_S,argv[2]);
				return 1;
			} else printf("%s written" ZT_EOL_S,argv[2]);
			if (argc >= 4) {
				idser = id.toString(false);
				if (!OSUtils::writeFile(argv[3],idser)) {
					fprintf(stderr,"Error writing to %s" ZT_EOL_S,argv[3]);
					return 1;
				} else printf("%s written" ZT_EOL_S,argv[3]);
			}
		} else printf("%s",idser.c_str());
	} else if (!strcmp(argv[1],"validate")) {
		if (argc < 3) {
			idtoolPrintHelp(stdout,argv[0]);
			return 1;
		}

		Identity id = getIdFromArg(argv[2]);
		if (!id) {
			fprintf(stderr,"Identity argument invalid or file unreadable: %s" ZT_EOL_S,argv[2]);
			return 1;
		}

		if (!id.locallyValidate()) {
			fprintf(stderr,"%s FAILED validation." ZT_EOL_S,argv[2]);
			return 1;
		} else printf("%s is a valid identity" ZT_EOL_S,argv[2]);
	} else if (!strcmp(argv[1],"getpublic")) {
		if (argc < 3) {
			idtoolPrintHelp(stdout,argv[0]);
			return 1;
		}

		Identity id = getIdFromArg(argv[2]);
		if (!id) {
			fprintf(stderr,"Identity argument invalid or file unreadable: %s" ZT_EOL_S,argv[2]);
			return 1;
		}

		printf("%s",id.toString(false).c_str());
	} else if (!strcmp(argv[1],"sign")) {
		if (argc < 4) {
			idtoolPrintHelp(stdout,argv[0]);
			return 1;
		}

		Identity id = getIdFromArg(argv[2]);
		if (!id) {
			fprintf(stderr,"Identity argument invalid or file unreadable: %s" ZT_EOL_S,argv[2]);
			return 1;
		}

		if (!id.hasPrivate()) {
			fprintf(stderr,"%s does not contain a private key (must use private to sign)" ZT_EOL_S,argv[2]);
			return 1;
		}

		std::string inf;
		if (!OSUtils::readFile(argv[3],inf)) {
			fprintf(stderr,"%s is not readable" ZT_EOL_S,argv[3]);
			return 1;
		}
		C25519::Signature signature = id.sign(inf.data(),(unsigned int)inf.length());
		printf("%s",Utils::hex(signature.data,(unsigned int)signature.size()).c_str());
	} else if (!strcmp(argv[1],"verify")) {
		if (argc < 4) {
			idtoolPrintHelp(stdout,argv[0]);
			return 1;
		}

		Identity id = getIdFromArg(argv[2]);
		if (!id) {
			fprintf(stderr,"Identity argument invalid or file unreadable: %s" ZT_EOL_S,argv[2]);
			return 1;
		}

		std::string inf;
		if (!OSUtils::readFile(argv[3],inf)) {
			fprintf(stderr,"%s is not readable" ZT_EOL_S,argv[3]);
			return 1;
		}

		std::string signature(Utils::unhex(argv[4]));
		if ((signature.length() > ZT_ADDRESS_LENGTH)&&(id.verify(inf.data(),(unsigned int)inf.length(),signature.data(),(unsigned int)signature.length()))) {
			printf("%s signature valid" ZT_EOL_S,argv[3]);
		} else {
			fprintf(stderr,"%s signature check FAILED" ZT_EOL_S,argv[3]);
			return 1;
		}
	} else if (!strcmp(argv[1],"initmoon")) {
		if (argc < 3) {
			idtoolPrintHelp(stdout,argv[0]);
		} else {
			const Identity id = getIdFromArg(argv[2]);
			if (!id) {
				fprintf(stderr,"%s is not a valid identity" ZT_EOL_S,argv[2]);
				return 1;
			}

			C25519::Pair kp(C25519::generate());

			nlohmann::json mj;
			mj["objtype"] = "world";
			mj["worldType"] = "moon";
			mj["updatesMustBeSignedBy"] = mj["signingKey"] = Utils::hex(kp.pub.data,(unsigned int)kp.pub.size());
			mj["signingKey_SECRET"] = Utils::hex(kp.priv.data,(unsigned int)kp.priv.size());
			mj["id"] = id.address().toString();
			nlohmann::json seedj;
			seedj["identity"] = id.toString(false);
			seedj["stableEndpoints"] = nlohmann::json::array();
			(mj["roots"] = nlohmann::json::array()).push_back(seedj);
			std::string mjd(OSUtils::jsonDump(mj));

			printf("%s" ZT_EOL_S,mjd.c_str());
		}
	} else if (!strcmp(argv[1],"genmoon")) {
		if (argc < 3) {
			idtoolPrintHelp(stdout,argv[0]);
		} else {
			std::string buf;
			if (!OSUtils::readFile(argv[2],buf)) {
				fprintf(stderr,"cannot read %s" ZT_EOL_S,argv[2]);
				return 1;
			}
			nlohmann::json mj(OSUtils::jsonParse(buf));

			const uint64_t id = Utils::hexStrToU64(OSUtils::jsonString(mj["id"],"0").c_str());
			if (!id) {
				fprintf(stderr,"ID in %s is invalid" ZT_EOL_S,argv[2]);
				return 1;
			}

			World::Type t;
			if (mj["worldType"] == "moon") {
				t = World::TYPE_MOON;
			} else if (mj["worldType"] == "planet") {
				t = World::TYPE_PLANET;
			} else {
				fprintf(stderr,"invalid worldType" ZT_EOL_S);
				return 1;
			}

			C25519::Pair signingKey;
			C25519::Public updatesMustBeSignedBy;
			Utils::unhex(OSUtils::jsonString(mj["signingKey"],""),signingKey.pub.data,(unsigned int)signingKey.pub.size());
			Utils::unhex(OSUtils::jsonString(mj["signingKey_SECRET"],""),signingKey.priv.data,(unsigned int)signingKey.priv.size());
			Utils::unhex(OSUtils::jsonString(mj["updatesMustBeSignedBy"],""),updatesMustBeSignedBy.data,(unsigned int)updatesMustBeSignedBy.size());

			std::vector<World::Root> roots;
			nlohmann::json &rootsj = mj["roots"];
			if (rootsj.is_array()) {
				for(unsigned long i=0;i<(unsigned long)rootsj.size();++i) {
					nlohmann::json &r = rootsj[i];
					if (r.is_object()) {
						roots.push_back(World::Root());
						roots.back().identity = Identity(OSUtils::jsonString(r["identity"],""));
						nlohmann::json &stableEndpointsj = r["stableEndpoints"];
						if (stableEndpointsj.is_array()) {
							for(unsigned long k=0;k<(unsigned long)stableEndpointsj.size();++k)
								roots.back().stableEndpoints.push_back(InetAddress(OSUtils::jsonString(stableEndpointsj[k],"")));
							std::sort(roots.back().stableEndpoints.begin(),roots.back().stableEndpoints.end());
						}
					}
				}
			}
			std::sort(roots.begin(),roots.end());

			const uint64_t now = OSUtils::now();
			World w(World::make(t,id,now,updatesMustBeSignedBy,roots,signingKey));
			Buffer<ZT_WORLD_MAX_SERIALIZED_LENGTH> wbuf;
			w.serialize(wbuf);
			char fn[128];
			Utils::snprintf(fn,sizeof(fn),"%.16llx.moon",w.id());
			OSUtils::writeFile(fn,wbuf.data(),wbuf.size());
			printf("wrote %s (signed world with timestamp %llu)" ZT_EOL_S,fn,(unsigned long long)now);
		}
	} else {
		idtoolPrintHelp(stdout,argv[0]);
		return 1;
	}

	return 0;
}

/****************************************************************************/
/* Unix helper functions and signal handlers                                */
/****************************************************************************/

#ifdef __UNIX_LIKE__
static void _sighandlerHup(int sig)
{
}
static void _sighandlerQuit(int sig)
{
	OneService *s = zt1Service;
	if (s)
		s->terminate();
	else exit(0);
}
#endif

// Drop privileges on Linux, if supported by libc etc. and "zerotier-one" user exists on system
#ifdef __LINUX__
#ifndef PR_CAP_AMBIENT
#define PR_CAP_AMBIENT 47
#define PR_CAP_AMBIENT_IS_SET 1
#define PR_CAP_AMBIENT_RAISE 2
#define PR_CAP_AMBIENT_LOWER 3
#define PR_CAP_AMBIENT_CLEAR_ALL 4
#endif
#define ZT_LINUX_USER "zerotier-one"
#define ZT_HAVE_DROP_PRIVILEGES 1
namespace {

// libc doesn't export capset, it is instead located in libcap
// We ignore libcap and call it manually.
struct cap_header_struct {
	__u32 version;
	int pid;
};
struct cap_data_struct {
	__u32 effective;
	__u32 permitted;
	__u32 inheritable;
};
static inline int _zt_capset(cap_header_struct* hdrp, cap_data_struct* datap) { return syscall(SYS_capset, hdrp, datap); }

static void _notDropping(const char *procName,const std::string &homeDir)
{
	struct stat buf;
	if (lstat(homeDir.c_str(),&buf) < 0) {
		if (buf.st_uid != 0 || buf.st_gid != 0) {
			fprintf(stderr, "%s: FATAL: failed to drop privileges and can't run as root since privileges were previously dropped (home directory not owned by root)" ZT_EOL_S,procName);
			exit(1);
		}
	}
	fprintf(stderr, "%s: WARNING: failed to drop privileges (kernel may not support required prctl features), running as root" ZT_EOL_S,procName);
}

static int _setCapabilities(int flags)
{
	cap_header_struct capheader = {_LINUX_CAPABILITY_VERSION_1, 0};
	cap_data_struct capdata;
	capdata.inheritable = capdata.permitted = capdata.effective = flags;
	return _zt_capset(&capheader, &capdata);
}

static void _recursiveChown(const char *path,uid_t uid,gid_t gid)
{
	struct dirent de;
	struct dirent *dptr;
	lchown(path,uid,gid);
	DIR *d = opendir(path);
	if (!d)
		return;
	dptr = (struct dirent *)0;
	for(;;) {
		if (readdir_r(d,&de,&dptr) != 0)
			break;
		if (!dptr)
			break;
		if ((strcmp(dptr->d_name,".") != 0)&&(strcmp(dptr->d_name,"..") != 0)&&(strlen(dptr->d_name) > 0)) {
			std::string p(path);
			p.push_back(ZT_PATH_SEPARATOR);
			p.append(dptr->d_name);
			_recursiveChown(p.c_str(),uid,gid); // will just fail and return on regular files
		}
	}
	closedir(d);
}

static void dropPrivileges(const char *procName,const std::string &homeDir)
{
	if (getuid() != 0)
		return;

	// dropPrivileges switches to zerotier-one user while retaining CAP_NET_ADMIN
	// and CAP_NET_RAW capabilities.
	struct passwd *targetUser = getpwnam(ZT_LINUX_USER);
	if (!targetUser)
		return;

	if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, CAP_NET_RAW, 0, 0) < 0) {
		// Kernel has no support for ambient capabilities.
		_notDropping(procName,homeDir);
		return;
	}
	if (prctl(PR_SET_SECUREBITS, SECBIT_KEEP_CAPS | SECBIT_NOROOT) < 0) {
		_notDropping(procName,homeDir);
		return;
	}

	// Change ownership of our home directory if everything looks good (does nothing if already chown'd)
	_recursiveChown(homeDir.c_str(),targetUser->pw_uid,targetUser->pw_gid);

	if (_setCapabilities((1 << CAP_NET_ADMIN) | (1 << CAP_NET_RAW) | (1 << CAP_SETUID) | (1 << CAP_SETGID)) < 0) {
		_notDropping(procName,homeDir);
		return;
	}

	int oldDumpable = prctl(PR_GET_DUMPABLE);
	if (prctl(PR_SET_DUMPABLE, 0) < 0) {
		// Disable ptracing. Otherwise there is a small window when previous
		// compromised ZeroTier process could ptrace us, when we still have CAP_SETUID.
		// (this is mitigated anyway on most distros by ptrace_scope=1)
		fprintf(stderr,"%s: FATAL: prctl(PR_SET_DUMPABLE) failed while attempting to relinquish root permissions" ZT_EOL_S,procName);
		exit(1);
	}

	// Relinquish root
	if (setgid(targetUser->pw_gid) < 0) {
		perror("setgid");
		exit(1);
	}
	if (setuid(targetUser->pw_uid) < 0) {
		perror("setuid");
		exit(1);
	}

	if (_setCapabilities((1 << CAP_NET_ADMIN) | (1 << CAP_NET_RAW)) < 0) {
		fprintf(stderr,"%s: FATAL: unable to drop capabilities after relinquishing root" ZT_EOL_S,procName);
		exit(1);
	}

	if (prctl(PR_SET_DUMPABLE, oldDumpable) < 0) {
		fprintf(stderr,"%s: FATAL: prctl(PR_SET_DUMPABLE) failed while attempting to relinquish root permissions" ZT_EOL_S,procName);
		exit(1);
	}

	if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_NET_ADMIN, 0, 0) < 0) {
		fprintf(stderr,"%s: FATAL: prctl(PR_CAP_AMBIENT,PR_CAP_AMBIENT_RAISE,CAP_NET_ADMIN) failed while attempting to relinquish root permissions" ZT_EOL_S,procName);
		exit(1);
	}
	if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_NET_RAW, 0, 0) < 0) {
		fprintf(stderr,"%s: FATAL: prctl(PR_CAP_AMBIENT,PR_CAP_AMBIENT_RAISE,CAP_NET_RAW) failed while attempting to relinquish root permissions" ZT_EOL_S,procName);
		exit(1);
	}
}

} // anonymous namespace
#endif // __LINUX__

/****************************************************************************/
/* Windows helper functions and signal handlers                             */
/****************************************************************************/

#ifdef __WINDOWS__
// Console signal handler routine to allow CTRL+C to work, mostly for testing
static BOOL WINAPI _winConsoleCtrlHandler(DWORD dwCtrlType)
{
	switch(dwCtrlType) {
		case CTRL_C_EVENT:
		case CTRL_BREAK_EVENT:
		case CTRL_CLOSE_EVENT:
		case CTRL_SHUTDOWN_EVENT:
			OneService *s = zt1Service;
			if (s)
				s->terminate();
			return TRUE;
	}
	return FALSE;
}

static void _winPokeAHole()
{
	char myPath[MAX_PATH];
	DWORD ps = GetModuleFileNameA(NULL,myPath,sizeof(myPath));
	if ((ps > 0)&&(ps < (DWORD)sizeof(myPath))) {
		STARTUPINFOA startupInfo;
		PROCESS_INFORMATION processInfo;

		startupInfo.cb = sizeof(startupInfo);
		memset(&startupInfo,0,sizeof(STARTUPINFOA));
		memset(&processInfo,0,sizeof(PROCESS_INFORMATION));
		if (CreateProcessA(NULL,(LPSTR)(std::string("C:\\Windows\\System32\\netsh.exe advfirewall firewall delete rule name=\"ZeroTier One\" program=\"") + myPath + "\"").c_str(),NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&startupInfo,&processInfo)) {
			WaitForSingleObject(processInfo.hProcess,INFINITE);
			CloseHandle(processInfo.hProcess);
			CloseHandle(processInfo.hThread);
		}

		startupInfo.cb = sizeof(startupInfo);
		memset(&startupInfo,0,sizeof(STARTUPINFOA));
		memset(&processInfo,0,sizeof(PROCESS_INFORMATION));
		if (CreateProcessA(NULL,(LPSTR)(std::string("C:\\Windows\\System32\\netsh.exe advfirewall firewall add rule name=\"ZeroTier One\" dir=in action=allow program=\"") + myPath + "\" enable=yes").c_str(),NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&startupInfo,&processInfo)) {
			WaitForSingleObject(processInfo.hProcess,INFINITE);
			CloseHandle(processInfo.hProcess);
			CloseHandle(processInfo.hThread);
		}

		startupInfo.cb = sizeof(startupInfo);
		memset(&startupInfo,0,sizeof(STARTUPINFOA));
		memset(&processInfo,0,sizeof(PROCESS_INFORMATION));
		if (CreateProcessA(NULL,(LPSTR)(std::string("C:\\Windows\\System32\\netsh.exe advfirewall firewall add rule name=\"ZeroTier One\" dir=out action=allow program=\"") + myPath + "\" enable=yes").c_str(),NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&startupInfo,&processInfo)) {
			WaitForSingleObject(processInfo.hProcess,INFINITE);
			CloseHandle(processInfo.hProcess);
			CloseHandle(processInfo.hThread);
		}
	}
}

// Returns true if this is running as the local administrator
static BOOL IsCurrentUserLocalAdministrator(void)
{
	BOOL   fReturn         = FALSE;
	DWORD  dwStatus;
	DWORD  dwAccessMask;
	DWORD  dwAccessDesired;
	DWORD  dwACLSize;
	DWORD  dwStructureSize = sizeof(PRIVILEGE_SET);
	PACL   pACL            = NULL;
	PSID   psidAdmin       = NULL;

	HANDLE hToken              = NULL;
	HANDLE hImpersonationToken = NULL;

	PRIVILEGE_SET   ps;
	GENERIC_MAPPING GenericMapping;

	PSECURITY_DESCRIPTOR     psdAdmin           = NULL;
	SID_IDENTIFIER_AUTHORITY SystemSidAuthority = SECURITY_NT_AUTHORITY;

	const DWORD ACCESS_READ  = 1;
	const DWORD ACCESS_WRITE = 2;

	__try
	{
		if (!OpenThreadToken(GetCurrentThread(), TOKEN_DUPLICATE|TOKEN_QUERY,TRUE,&hToken))
		{
			if (GetLastError() != ERROR_NO_TOKEN)
				__leave;
			if (!OpenProcessToken(GetCurrentProcess(),TOKEN_DUPLICATE|TOKEN_QUERY, &hToken))
				__leave;
		}
		if (!DuplicateToken (hToken, SecurityImpersonation,&hImpersonationToken))
			__leave;
		if (!AllocateAndInitializeSid(&SystemSidAuthority, 2,
			SECURITY_BUILTIN_DOMAIN_RID,
			DOMAIN_ALIAS_RID_ADMINS,
			0, 0, 0, 0, 0, 0, &psidAdmin))
			__leave;
		psdAdmin = LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
		if (psdAdmin == NULL)
			__leave;
		if (!InitializeSecurityDescriptor(psdAdmin,SECURITY_DESCRIPTOR_REVISION))
			__leave;
		dwACLSize = sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) + GetLengthSid(psidAdmin) - sizeof(DWORD);
		pACL = (PACL)LocalAlloc(LPTR, dwACLSize);
		if (pACL == NULL)
			__leave;
		if (!InitializeAcl(pACL, dwACLSize, ACL_REVISION2))
			__leave;
		dwAccessMask= ACCESS_READ | ACCESS_WRITE;
		if (!AddAccessAllowedAce(pACL, ACL_REVISION2, dwAccessMask, psidAdmin))
			__leave;
		if (!SetSecurityDescriptorDacl(psdAdmin, TRUE, pACL, FALSE))
			__leave;

		SetSecurityDescriptorGroup(psdAdmin, psidAdmin, FALSE);
		SetSecurityDescriptorOwner(psdAdmin, psidAdmin, FALSE);

		if (!IsValidSecurityDescriptor(psdAdmin))
			__leave;
		dwAccessDesired = ACCESS_READ;

		GenericMapping.GenericRead    = ACCESS_READ;
		GenericMapping.GenericWrite   = ACCESS_WRITE;
		GenericMapping.GenericExecute = 0;
		GenericMapping.GenericAll     = ACCESS_READ | ACCESS_WRITE;

		if (!AccessCheck(psdAdmin, hImpersonationToken, dwAccessDesired,
			&GenericMapping, &ps, &dwStructureSize, &dwStatus,
			&fReturn))
		{
			fReturn = FALSE;
			__leave;
		}
	}
	__finally
	{
		// Clean up.
		if (pACL) LocalFree(pACL);
		if (psdAdmin) LocalFree(psdAdmin);
		if (psidAdmin) FreeSid(psidAdmin);
		if (hImpersonationToken) CloseHandle (hImpersonationToken);
		if (hToken) CloseHandle (hToken);
	}

	return fReturn;
}
#endif // __WINDOWS__

/****************************************************************************/
/* main() and friends                                                       */
/****************************************************************************/

static void printHelp(const char *cn,FILE *out)
{
	fprintf(out,
		"%s version %d.%d.%d" ZT_EOL_S,
		PROGRAM_NAME,
		ZEROTIER_ONE_VERSION_MAJOR, ZEROTIER_ONE_VERSION_MINOR, ZEROTIER_ONE_VERSION_REVISION);
	fprintf(out,
		COPYRIGHT_NOTICE ZT_EOL_S
		LICENSE_GRANT ZT_EOL_S);
	fprintf(out,"Usage: %s [-switches] [home directory]" ZT_EOL_S"" ZT_EOL_S,cn);
	fprintf(out,"Available switches:" ZT_EOL_S);
	fprintf(out,"  -h                - Display this help" ZT_EOL_S);
	fprintf(out,"  -v                - Show version" ZT_EOL_S);
	fprintf(out,"  -U                - Skip privilege check and do not attempt to drop privileges" ZT_EOL_S);
	fprintf(out,"  -p<port>          - Port for UDP and TCP/HTTP (default: 9993, 0 for random)" ZT_EOL_S);

#ifdef __UNIX_LIKE__
	fprintf(out,"  -d                - Fork and run as daemon (Unix-ish OSes)" ZT_EOL_S);
#endif // __UNIX_LIKE__

#ifdef __WINDOWS__
	fprintf(out,"  -C                - Run from command line instead of as service (Windows)" ZT_EOL_S);
	fprintf(out,"  -I                - Install Windows service (Windows)" ZT_EOL_S);
	fprintf(out,"  -R                - Uninstall Windows service (Windows)" ZT_EOL_S);
	fprintf(out,"  -D                - Remove all instances of Windows tap device (Windows)" ZT_EOL_S);
#endif // __WINDOWS__

	fprintf(out,"  -i                - Generate and manage identities (zerotier-idtool)" ZT_EOL_S);
	fprintf(out,"  -q                - Query API (zerotier-cli)" ZT_EOL_S);
}

#ifdef __WINDOWS__
int _tmain(int argc, _TCHAR* argv[])
#else
int main(int argc,char **argv)
#endif
{
#ifdef __UNIX_LIKE__
	signal(SIGHUP,&_sighandlerHup);
	signal(SIGPIPE,SIG_IGN);
	signal(SIGUSR1,SIG_IGN);
	signal(SIGUSR2,SIG_IGN);
	signal(SIGALRM,SIG_IGN);
	signal(SIGINT,&_sighandlerQuit);
	signal(SIGTERM,&_sighandlerQuit);
	signal(SIGQUIT,&_sighandlerQuit);

	/* Ensure that there are no inherited file descriptors open from a previous
	 * incarnation. This is a hack to ensure that GitHub issue #61 or variants
	 * of it do not return, and should not do anything otherwise bad. */
	{
		int mfd = STDIN_FILENO;
		if (STDOUT_FILENO > mfd) mfd = STDOUT_FILENO;
		if (STDERR_FILENO > mfd) mfd = STDERR_FILENO;
		for(int f=mfd+1;f<1024;++f)
			::close(f);
	}

	bool runAsDaemon = false;
#endif // __UNIX_LIKE__

#ifdef __WINDOWS__
	{
		WSADATA wsaData;
		WSAStartup(MAKEWORD(2,2),&wsaData);
	}

#ifdef ZT_WIN_RUN_IN_CONSOLE
	bool winRunFromCommandLine = true;
#else
	bool winRunFromCommandLine = false;
#endif
#endif // __WINDOWS__

	if ((strstr(argv[0],"zerotier-idtool"))||(strstr(argv[0],"ZEROTIER-IDTOOL")))
		return idtool(argc,argv);
	if ((strstr(argv[0],"zerotier-cli"))||(strstr(argv[0],"ZEROTIER-CLI")))
		return cli(argc,argv);

	std::string homeDir;
	unsigned int port = ZT_DEFAULT_PORT;
	bool skipRootCheck = false;

	for(int i=1;i<argc;++i) {
		if (argv[i][0] == '-') {
			switch(argv[i][1]) {

				case 'p': // port -- for both UDP and TCP, packets and control plane
					port = Utils::strToUInt(argv[i] + 2);
					if (port > 0xffff) {
						printHelp(argv[0],stdout);
						return 1;
					}
					break;

#ifdef __UNIX_LIKE__
				case 'd': // Run in background as daemon
					runAsDaemon = true;
					break;
#endif // __UNIX_LIKE__

				case 'U':
					skipRootCheck = true;
					break;

				case 'v': // Display version
					printf("%d.%d.%d" ZT_EOL_S,ZEROTIER_ONE_VERSION_MAJOR,ZEROTIER_ONE_VERSION_MINOR,ZEROTIER_ONE_VERSION_REVISION);
					return 0;

				case 'i': // Invoke idtool personality
					if (argv[i][2]) {
						printHelp(argv[0],stdout);
						return 0;
					} else return idtool(argc,argv);

				case 'q': // Invoke cli personality
					if (argv[i][2]) {
						printHelp(argv[0],stdout);
						return 0;
					} else return cli(argc,argv);

#ifdef __WINDOWS__
				case 'C': // Run from command line instead of as Windows service
					winRunFromCommandLine = true;
					break;

				case 'I': { // Install this binary as a Windows service
						if (IsCurrentUserLocalAdministrator() != TRUE) {
							fprintf(stderr,"%s: must be run as a local administrator." ZT_EOL_S,argv[0]);
							return 1;
						}
						std::string ret(InstallService(ZT_SERVICE_NAME,ZT_SERVICE_DISPLAY_NAME,ZT_SERVICE_START_TYPE,ZT_SERVICE_DEPENDENCIES,ZT_SERVICE_ACCOUNT,ZT_SERVICE_PASSWORD));
						if (ret.length()) {
							fprintf(stderr,"%s: unable to install service: %s" ZT_EOL_S,argv[0],ret.c_str());
							return 3;
						}
						return 0;
					} break;

				case 'R': { // Uninstall this binary as Windows service
						if (IsCurrentUserLocalAdministrator() != TRUE) {
							fprintf(stderr,"%s: must be run as a local administrator." ZT_EOL_S,argv[0]);
							return 1;
						}
						std::string ret(UninstallService(ZT_SERVICE_NAME));
						if (ret.length()) {
							fprintf(stderr,"%s: unable to uninstall service: %s" ZT_EOL_S,argv[0],ret.c_str());
							return 3;
						}
						return 0;
					} break;

				case 'D': {
						std::string err = WindowsEthernetTap::destroyAllPersistentTapDevices();
						if (err.length() > 0) {
							fprintf(stderr,"%s: unable to uninstall one or more persistent tap devices: %s" ZT_EOL_S,argv[0],err.c_str());
							return 3;
						}
						return 0;
					} break;
#endif // __WINDOWS__

				case 'h':
				case '?':
				default:
					printHelp(argv[0],stdout);
					return 0;
			}
		} else {
			if (homeDir.length()) {
				printHelp(argv[0],stdout);
				return 0;
			} else {
				homeDir = argv[i];
			}
		}
	}

	if (!homeDir.length())
		homeDir = OneService::platformDefaultHomePath();
	if (!homeDir.length()) {
		fprintf(stderr,"%s: no home path specified and no platform default available" ZT_EOL_S,argv[0]);
		return 1;
	} else {
		std::vector<std::string> hpsp(OSUtils::split(homeDir.c_str(),ZT_PATH_SEPARATOR_S,"",""));
		std::string ptmp;
		if (homeDir[0] == ZT_PATH_SEPARATOR)
			ptmp.push_back(ZT_PATH_SEPARATOR);
		for(std::vector<std::string>::iterator pi(hpsp.begin());pi!=hpsp.end();++pi) {
			if (ptmp.length() > 0)
				ptmp.push_back(ZT_PATH_SEPARATOR);
			ptmp.append(*pi);
			if ((*pi != ".")&&(*pi != "..")) {
				if (!OSUtils::mkdir(ptmp))
					throw std::runtime_error("home path does not exist, and could not create");
			}
		}
	}

	// This can be removed once the new controller code has been around for many versions
	if (OSUtils::fileExists((homeDir + ZT_PATH_SEPARATOR_S + "controller.db").c_str(),true)) {
		fprintf(stderr,"%s: FATAL: an old controller.db exists in %s -- see instructions in controller/README.md for how to migrate!" ZT_EOL_S,argv[0],homeDir.c_str());
		return 1;
	}

#ifdef __UNIX_LIKE__
#ifndef ZT_ONE_NO_ROOT_CHECK
	if ((!skipRootCheck)&&(getuid() != 0)) {
		fprintf(stderr,"%s: must be run as root (uid 0)" ZT_EOL_S,argv[0]);
		return 1;
	}
#endif // !ZT_ONE_NO_ROOT_CHECK
	if (runAsDaemon) {
		long p = (long)fork();
		if (p < 0) {
			fprintf(stderr,"%s: could not fork" ZT_EOL_S,argv[0]);
			return 1;
		} else if (p > 0)
			return 0; // forked
		// else p == 0, so we are daemonized
	}
#endif // __UNIX_LIKE__

#ifdef __WINDOWS__
	// Uninstall legacy tap devices. New devices will automatically be installed and configured
	// when tap instances are created.
	WindowsEthernetTap::destroyAllLegacyPersistentTapDevices();

	if (winRunFromCommandLine) {
		// Running in "interactive" mode (mostly for debugging)
		if (IsCurrentUserLocalAdministrator() != TRUE) {
			if (!skipRootCheck) {
				fprintf(stderr,"%s: must be run as a local administrator." ZT_EOL_S,argv[0]);
				return 1;
			}
		} else {
			_winPokeAHole();
		}
		SetConsoleCtrlHandler(&_winConsoleCtrlHandler,TRUE);
		// continues on to ordinary command line execution code below...
	} else {
		// Running from service manager
		_winPokeAHole();
		ZeroTierOneService zt1Service;
		if (CServiceBase::Run(zt1Service) == TRUE) {
			return 0;
		} else {
			fprintf(stderr,"%s: unable to start service (try -h for help)" ZT_EOL_S,argv[0]);
			return 1;
		}
	}
#endif // __WINDOWS__

#ifdef __UNIX_LIKE__

#ifdef ZT_HAVE_DROP_PRIVILEGES
	dropPrivileges(argv[0],homeDir);
#endif

	std::string pidPath(homeDir + ZT_PATH_SEPARATOR_S + ZT_PID_PATH);
	{
		// Write .pid file to home folder
		FILE *pf = fopen(pidPath.c_str(),"w");
		if (pf) {
			fprintf(pf,"%ld",(long)getpid());
			fclose(pf);
		}
	}
#endif // __UNIX_LIKE__

	unsigned int returnValue = 0;

	for(;;) {
		zt1Service = OneService::newInstance(homeDir.c_str(),port);
		switch(zt1Service->run()) {
			case OneService::ONE_STILL_RUNNING: // shouldn't happen, run() won't return until done
			case OneService::ONE_NORMAL_TERMINATION:
				break;
			case OneService::ONE_UNRECOVERABLE_ERROR:
				fprintf(stderr,"%s: fatal error: %s" ZT_EOL_S,argv[0],zt1Service->fatalErrorMessage().c_str());
				returnValue = 1;
				break;
			case OneService::ONE_IDENTITY_COLLISION: {
				delete zt1Service;
				zt1Service = (OneService *)0;
				std::string oldid;
				OSUtils::readFile((homeDir + ZT_PATH_SEPARATOR_S + "identity.secret").c_str(),oldid);
				if (oldid.length()) {
					OSUtils::writeFile((homeDir + ZT_PATH_SEPARATOR_S + "identity.secret.saved_after_collision").c_str(),oldid);
					OSUtils::rm((homeDir + ZT_PATH_SEPARATOR_S + "identity.secret").c_str());
					OSUtils::rm((homeDir + ZT_PATH_SEPARATOR_S + "identity.public").c_str());
				}
			}	continue; // restart!
		}
		break; // terminate loop -- normally we don't keep restarting
	}

	delete zt1Service;
	zt1Service = (OneService *)0;

	return returnValue;
}
