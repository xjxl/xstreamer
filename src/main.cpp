/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** main.cpp
**
** -------------------------------------------------------------------------*/

#include <signal.h>

#include <iostream>
#include <fstream>

#include "rtc_base/ssl_adapter.h"
#include "rtc_base/thread.h"
#include "p2p/base/stun_server.h"
#include "p2p/base/basic_packet_socket_factory.h"
#include "p2p/base/turn_server.h"

#include "system_wrappers/include/field_trial.h"

#include "PeerConnectionManager.h"
#include "HttpServerRequestHandler.h"

#if WIN32
#include "getopt.h"
#endif

PeerConnectionManager* webRtcServer = NULL;

void sighandler(int n)
{
	printf("SIGINT\n");
	// delete need thread still running
	delete webRtcServer;
	webRtcServer = NULL;
	rtc::Thread::Current()->Quit(); 
}

class TurnAuth : public cricket::TurnAuthInterface {
	public:
		virtual bool GetKey(absl::string_view username,absl::string_view realm, std::string* key) { 
			return cricket::ComputeStunCredentialHash(std::string(username), std::string(realm), std::string(username), key); 
		}
};

class TurnRedirector : public cricket::TurnRedirectInterface
{
public:
	explicit TurnRedirector() {}

	virtual bool ShouldRedirect(const rtc::SocketAddress &, rtc::SocketAddress *out)
	{
		return true;
	}
};

/* ---------------------------------------------------------------------------
**  main
** -------------------------------------------------------------------------*/
int main(int argc, char* argv[])
{
	const char* turnurl       = "";
	const char* defaultlocalstunurl  = "0.0.0.0:3478";
	const char* localstunurl  = NULL;
	const char* defaultlocalturnurl  = "turn:turn@0.0.0.0:3478";
	const char* localturnurl  = NULL;
	const char* stunurl       = "stun.l.google.com:19302";
	std::string localWebrtcUdpPortRange = "0:65535";
	int logLevel              = rtc::LS_ERROR;
	const char* webroot       = "./www";
	std::string sslCertificate;
	webrtc::AudioDeviceModule::AudioLayer audioLayer = webrtc::AudioDeviceModule::kPlatformDefaultAudio;
	std::string nbthreads;
	std::string passwdFile;
	std::string authDomain = "mydomain.com";
	bool        disableXframeOptions = false;

	std::string publishFilter(".*");
	Json::Value config;  
	bool        useNullCodec = false;
	bool        usePlanB = false;
	int         maxpc = 0;
	webrtc::PeerConnectionInterface::IceTransportsType transportType = webrtc::PeerConnectionInterface::IceTransportsType::kAll;
	std::string webrtcTrialsFields = "WebRTC-FrameDropper/Disabled/";
	TurnAuth 	turnAuth;
	TurnRedirector turnRedirector;

	std::string httpAddress("0.0.0.0:");
	std::string httpPort = "9990";
	const char * port = getenv("PORT");
	if (port)
	{
		httpPort = port;
	}
	httpAddress.append(httpPort);

	std::string streamName;
	int c = 0;
	while ((c = getopt (argc, argv, "hVv::C:" "c:H:w:N:A:D:Xm:I:" "T::t:S::s::R:W:" "a::q:ob" "n:u:U:")) != -1)
	{
		switch (c)
		{
			case 'H': httpAddress = optarg;        break;
			case 'c': sslCertificate = optarg;     break;
			case 'w': webroot = optarg;            break;
			case 'N': nbthreads = optarg;          break;
			case 'A': passwdFile = optarg;         break;
			case 'D': authDomain = optarg;         break;
			case 'X': disableXframeOptions = true; break;
			case 'm': maxpc = atoi(optarg);        break;
			case 'I': transportType = (webrtc::PeerConnectionInterface::IceTransportsType)atoi(optarg);break;

			case 'T': localturnurl = optarg ? optarg : defaultlocalturnurl; turnurl = localturnurl; break;
			case 't': turnurl = optarg;                                                             break;
			case 'S': localstunurl = optarg ? optarg : defaultlocalstunurl; stunurl = localstunurl; break;
			case 's': stunurl = optarg ? optarg : defaultlocalstunurl;                              break;
			case 'R': localWebrtcUdpPortRange = optarg;                                             break;
			case 'W': webrtcTrialsFields = optarg;                                                  break;

			case 'a': audioLayer = optarg ? (webrtc::AudioDeviceModule::AudioLayer)atoi(optarg) : webrtc::AudioDeviceModule::kDummyAudio; break;	
			case 'q': publishFilter = optarg ; break;
			case 'o': useNullCodec = true; break;
			case 'b': usePlanB = true; break;
				
			case 'C': {
				std::ifstream stream(optarg);
				stream >> config;
				break;
			}

			case 'n': streamName = optarg; break;
			case 'u': {
				if (!streamName.empty()) {
					config["urls"][streamName]["video"] = optarg; 
				}
			}
			break;
			case 'U': {
				if (!streamName.empty()) {
					config["urls"][streamName]["audio"] = optarg; 
				}
			}
			break;
			
			case 'v': 
			break;			
			case 'V':
				exit(0);
			break;
			case 'h':
			default:
				exit(0);
		}
	}

	while (optind<argc)
	{
		std::string url(argv[optind]);
		config["urls"][url]["video"] = url; 
		optind++;
	}

	rtc::LogMessage::LogToDebug((rtc::LoggingSeverity)logLevel);
	rtc::LogMessage::LogTimestamps();
	rtc::LogMessage::LogThreads();

	rtc::ThreadManager::Instance()->WrapCurrentThread();
	rtc::Thread* thread = rtc::Thread::Current();
	rtc::InitializeSSL();

	// webrtc server
	std::list<std::string> iceServerList;
	if ((strlen(stunurl) != 0) && (strcmp(stunurl,"-") != 0)) {
		iceServerList.push_back(std::string("stun:")+stunurl);
	}
	if (strlen(turnurl)) {
		iceServerList.push_back(std::string("turn:")+turnurl);
	}

	// init trials fields
	webrtc::field_trial::InitFieldTrialsFromString(webrtcTrialsFields.c_str());

	webRtcServer = new PeerConnectionManager(iceServerList, config["urls"], audioLayer, publishFilter, localWebrtcUdpPortRange, useNullCodec, usePlanB, maxpc, transportType);
	if (!webRtcServer->InitializePeerConnection())
	{
		std::cout << "Cannot Initialize WebRTC server" << std::endl;
	}
	else
	{
		// http server
		std::vector<std::string> options;
		options.push_back("document_root");
		options.push_back(webroot);
		options.push_back("enable_directory_listing");
		options.push_back("no");
		if (!disableXframeOptions) {
			options.push_back("additional_header");
			options.push_back("X-Frame-Options: SAMEORIGIN");
		}
		options.push_back("access_control_allow_origin");
		options.push_back("*");
		options.push_back("listening_ports");
		options.push_back(httpAddress);
		options.push_back("enable_keep_alive");
		options.push_back("yes");
		options.push_back("keep_alive_timeout_ms");
		options.push_back("1000");
		options.push_back("decode_url");
		options.push_back("no");
		if (!sslCertificate.empty()) {
			options.push_back("ssl_certificate");
			options.push_back(sslCertificate);
		}
		if (!nbthreads.empty()) {
			options.push_back("num_threads");
			options.push_back(nbthreads);
		}
		if (!passwdFile.empty()) {
			options.push_back("global_auth_file");
			options.push_back(passwdFile);
			options.push_back("authentication_domain");
			options.push_back(authDomain);
		}
		
		try {
			std::map<std::string,HttpServerRequestHandler::httpFunction> func = webRtcServer->getHttpApi();
			std::cout << "Listen at " << httpAddress << std::endl;
			HttpServerRequestHandler httpServer(func, options);

			// start STUN server if needed
			std::unique_ptr<cricket::StunServer> stunserver;
			if (localstunurl != NULL)
			{
				rtc::SocketAddress server_addr;
				server_addr.FromString(localstunurl);
				rtc::AsyncUDPSocket* server_socket = rtc::AsyncUDPSocket::Create(thread->socketserver(), server_addr);
				if (server_socket)
				{
					stunserver.reset(new cricket::StunServer(server_socket));
					std::cout << "STUN Listening at " << server_addr.ToString() << std::endl;
				}
			}

			// start TRUN server if needed
			std::unique_ptr<cricket::TurnServer> turnserver;
			if (localturnurl != NULL)
			{
				std::istringstream is(localturnurl);
				std::string addr;
				std::getline(is, addr, '@');
				std::getline(is, addr, '@');
				rtc::SocketAddress server_addr;
				server_addr.FromString(addr);
				turnserver.reset(new cricket::TurnServer(rtc::Thread::Current()));
				turnserver->set_auth_hook(&turnAuth);
				turnserver->set_redirect_hook(&turnRedirector);

				rtc::Socket* tcp_server_socket = thread->socketserver()->CreateSocket(AF_INET, SOCK_STREAM);
				if (tcp_server_socket) {
					std::cout << "TURN Listening TCP at " << server_addr.ToString() << std::endl;
					tcp_server_socket->Bind(server_addr);
					tcp_server_socket->Listen(5);
					turnserver->AddInternalServerSocket(tcp_server_socket, cricket::PROTO_TCP);
				} else {
					std::cout << "Failed to create TURN TCP server socket" << std::endl;
				}

				rtc::AsyncUDPSocket* udp_server_socket = rtc::AsyncUDPSocket::Create(thread->socketserver(), server_addr);
				if (udp_server_socket) {
					std::cout << "TURN Listening UDP at " << server_addr.ToString() << std::endl;
					turnserver->AddInternalSocket(udp_server_socket, cricket::PROTO_UDP);
				} else {
					std::cout << "Failed to create TURN UDP server socket" << std::endl;
				}

				is.clear();
				is.str(turnurl);
				std::getline(is, addr, '@');
				std::getline(is, addr, '@');
				rtc::SocketAddress external_server_addr;
				external_server_addr.FromString(addr);		
				std::cout << "TURN external addr:" << external_server_addr.ToString() << std::endl;			
				turnserver->SetExternalSocketFactory(new rtc::BasicPacketSocketFactory(thread->socketserver()), rtc::SocketAddress(external_server_addr.ipaddr(), 0));
			}
			
			// mainloop
			signal(SIGINT,sighandler);
			thread->Run();

		} catch (const CivetException & ex) {
			std::cout << "Cannot Initialize start HTTP server exception:" << ex.what() << std::endl;
		}
	}

	rtc::CleanupSSL();
	std::cout << "Exit" << std::endl;
	return 0;
}

