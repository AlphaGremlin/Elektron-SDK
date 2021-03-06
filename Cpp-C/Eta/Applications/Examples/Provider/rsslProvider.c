

/*
 * This is the main file for the rsslProvider application.  It is a
 * single-threaded server application.  The application uses either
 * the operating parameters entered by the user or a default set of
 * parameters.  See readme file for usage.
 *
 * The purpose of this application is to provide level 1 market price
 * data to one or more consumers.  First the application initializes
 * the RSSL transport and binds the server.  After that, it attempts
 * to load the dictionary from a file.  If the dictionary cannot be
 * loaded, the application exits.  Finally, it processes login, source
 * directory, dictionary, and market price requests from consumers and
 * sends the appropriate responses.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>

#include "rsslProvider.h"
#include "rsslLoginHandler.h"
#include "rsslDirectoryHandler.h"
#include "rsslDictionaryProvider.h"
#include "rsslItemHandler.h"
#include "rsslSendMessage.h"

static fd_set	readfds;
static fd_set	exceptfds;
fd_set	wrtfds; /* used by sendMessage() */
RsslError error;
RsslServer *rsslSrvr;
static char portNo[128];
static char serviceName[128];
static char traceOutputFile[128];
static RsslInt32 timeToRun = 1200;
static time_t rsslProviderRuntime = 0;
static RsslUInt32 clientSessionCount;
static RsslBool xmlTrace = RSSL_FALSE;
RsslBool showTransportDetails = RSSL_FALSE;
static RsslReadOutArgs readOutArgs;

static RsslClientSessionInfo clientSessions[MAX_CLIENT_SESSIONS];

/* default port number */
static const char *defaultPortNo = "14002";
/* default service name */
static const char *defaultServiceName = "DIRECT_FEED";

void exitWithUsage()
{
	printf(	"Usage: -p <port number> -s <service name> -id <service ID> -runtime <seconds>\n");
#ifdef _WIN32
		printf("\nPress Enter or Return key to exit application:");
		getchar();
#endif
	exit(-1);
}

int main(int argc, char **argv)
{
	int i;
	struct timeval time_interval;
	RsslError error;
	fd_set useRead;
	fd_set useExcept;
	fd_set useWrt;
	int selRet;
	char errTxt[256];
	RsslBuffer errorText = {255, (char*)errTxt};
	RsslInProgInfo inProg = RSSL_INIT_IN_PROG_INFO;
	RsslRet	retval = 0;
	int iargs;
	rsslClearReadOutArgs(&readOutArgs);
	snprintf(portNo, 128, "%s", defaultPortNo);
	snprintf(serviceName, 128, "%s", defaultServiceName);
	setServiceId(1);
	for(iargs = 1; iargs < argc; ++iargs)
	{
		if (0 == strcmp("-p", argv[iargs]))
		{
			++iargs; if (iargs == argc) exitWithUsage();
			snprintf(portNo, 128, "%s", argv[iargs]);
		}
		else if (0 == strcmp("-s", argv[iargs]))
		{
			++iargs; if (iargs == argc) exitWithUsage();
			snprintf(serviceName, 128, "%s", argv[iargs]);
		}
		else if (0 == strcmp("-id", argv[iargs]))
		{
			long tmpId = 0;
			++iargs; if (iargs == argc) exitWithUsage();
			tmpId = atol(argv[iargs]);
			if (tmpId < 0)
			{
				printf("ServiceId must be positive.\n");
				exitWithUsage();
			}
			setServiceId(tmpId);
		}
		else if (0 == strcmp("-x", argv[iargs]))
		{
			xmlTrace = RSSL_TRUE;
			snprintf(traceOutputFile, 128, "RsslProvider\0");
		}
		else if (0 == strcmp("-td", argv[iargs]))
		{
			showTransportDetails = RSSL_TRUE;
		}
		else if(0 == strcmp("-runtime", argv[iargs]))
		{
			++iargs; if (iargs == argc) exitWithUsage();
			timeToRun = atoi(argv[iargs]);
		}
		else
		{
			printf("Error: Unrecognized option: %s\n\n", argv[iargs]);
			exitWithUsage();
		}
	}
	printf("portNo: %s\n", portNo);
	printf("serviceName: %s\n", serviceName);
	printf("serviceId: %llu\n", getServiceId());

	/* Initialiize client session information */
	clientSessionCount = 0;
	for (i = 0; i < NUM_CLIENT_SESSIONS; i++)
	{
		clearClientSessionInfo(&clientSessions[i]);
	}

	/* Initialize login handler */
	initLoginHandler();

	/* Initialize source directory handler */
	initDirectoryHandler();

	/* Initialize dictionary provider */
	initDictionaryProvider();

	/* Initialize market price handler */
	initItemHandler();

	/* Initialize symbol list item list */
	initSymbolListItemList();

	initMarketByOrderItems();

	/* set service name in directory handler */
	setServiceName(serviceName);

	/* try to load local dictionary */
	if (loadDictionary() != RSSL_RET_SUCCESS)
	{
		/* if no local dictionary found maybe we can request it from ADH */
		printf("\nNo local dictionary found, will try to request it from ADH if it supports the Provider Dictionary Download\n");
	}

	/* Initialize run-time */
	initRuntime();


	/* Initialize RSSL */
	/* RSSL_LOCK_NONE is used since this is a single threaded application. */
	if (rsslInitialize(RSSL_LOCK_NONE, &error) != RSSL_RET_SUCCESS)
	{
		printf("rsslInitialize(): failed <%s>\n",error.text);
		/* WINDOWS: wait for user to enter something before exiting  */
#ifdef _WIN32
		printf("\nPress Enter or Return key to exit application:");
		getchar();
#endif
		exit(RSSL_RET_FAILURE);
	}

	FD_ZERO(&readfds);
	FD_ZERO(&exceptfds);
	FD_ZERO(&wrtfds);

	/* Bind RSSL server */
	if ((rsslSrvr = bindRsslServer(portNo, &error)) == NULL)
	{
		printf("Unable to bind RSSL server: <%s>\n",error.text);
		/* WINDOWS: wait for user to enter something before exiting  */
#ifdef _WIN32
		printf("\nPress Enter or Return key to exit application:");
		getchar();
#endif
		exit(RSSL_RET_FAILURE);
	}

	/* this is the main loop */
	while(1)
	{
		useRead = readfds;
		useExcept = exceptfds;
		useWrt = wrtfds;
		time_interval.tv_sec = UPDATE_INTERVAL;
		time_interval.tv_usec = 0;

		/* Call select() to check for any messages */
		selRet = select(FD_SETSIZE,&useRead,
		    &useWrt,&useExcept,&time_interval);

		if (selRet == 0) /* no messages received, send updates and continue */
		{
			/* Send market price updates for each connected channel */
			updateItemInfo();
			for (i = 0; i < NUM_CLIENT_SESSIONS; i++)
			{
				if ((clientSessions[i].clientChannel != NULL) && (clientSessions[i].clientChannel->socketId != -1))
				{
					if (sendItemUpdates(clientSessions[i].clientChannel) != RSSL_RET_SUCCESS)
					{
						removeClientSessionForChannel(clientSessions[i].clientChannel);
					}
				}
			}
		}
		else if (selRet > 0) /* messages received, create new client session or read from channel */
		{
			if ((rsslSrvr != NULL) && (rsslSrvr->socketId != -1) && (FD_ISSET(rsslSrvr->socketId,&useRead)))
			{
				createNewClientSession(rsslSrvr);
			}

			for (i = 0; i < CLIENT_SESSIONS_LIMIT; i++)
			{
				if ((clientSessions[i].clientChannel != NULL) && (clientSessions[i].clientChannel->socketId != -1))
				{
					if ((FD_ISSET(clientSessions[i].clientChannel->socketId, &useRead)) ||
						(FD_ISSET(clientSessions[i].clientChannel->socketId, &useExcept)))
					{
						readFromChannel(clientSessions[i].clientChannel);
					}

					/* flush for write file descriptor and active state */
					if (clientSessions[i].clientChannel != NULL &&
						FD_ISSET(clientSessions[i].clientChannel->socketId, &useWrt) &&
						clientSessions[i].clientChannel->state == RSSL_CH_STATE_ACTIVE)
					{
						
						/* if the channel just changed to active, we need to update our ping timeouts as it 
						   may be smaller than what we set due to the negotiation */
						if (clientSessions[i].pingsInitialized != RSSL_TRUE)
						{
							initPingTimes(&clientSessions[i]);
							printf("Using %d as pingTimeout for Channel "SOCKET_PRINT_TYPE"\n", clientSessions[i].clientChannel->pingTimeout, clientSessions[i].clientChannel->socketId);
						}

						if ((retval = rsslFlush(clientSessions[i].clientChannel, &error)) < RSSL_RET_SUCCESS)
						{
							printf("rsslFlush() failed with return code %d - <%s>\n", retval, error.text);
							removeClientSessionForChannel(clientSessions[i].clientChannel);
						}
						else if (retval == RSSL_RET_SUCCESS)
						{
							/* clear write fd */
							FD_CLR(clientSessions[i].clientChannel->socketId, &wrtfds);
						}
					}
				}
			}
		}
		else /* error */
		{
#ifdef _WIN32
			if (WSAGetLastError() == WSAEINTR)
			{
				continue;
			}
			else
			{
				printf("select() failed with error code %d\n", WSAGetLastError());
				break;
			}
#else
			if (errno == EINTR)
			{
				continue;
			}
			else
			{
				printf("select() failed with error code %d\n", errno);
				break;
			}
#endif
		}

		/* Handle pings */
		handlePings();

		/* Handle run-time */
		handleRuntime();
	}
}

/*
 * Reads from a channel.
 * chnl - The channel to be read from
 */
static void readFromChannel(RsslChannel* chnl)
{
	int			retval;
	RsslError	error;
	RsslBuffer *msgBuf=0;
	RsslRet		readret;
	RsslReadInArgs readInArgs;
	RsslInProgInfo inProg = RSSL_INIT_IN_PROG_INFO;
#ifdef _WIN32
	int rcvBfrSize = 65535;
	int sendBfrSize = 65535;
#endif

	if(chnl->socketId != -1 && chnl->state == RSSL_CH_STATE_INITIALIZING)
	{
		if ((retval = rsslInitChannel(chnl, &inProg, &error)) < RSSL_RET_SUCCESS)
		{
			printf("\nsessionInactive fd="SOCKET_PRINT_TYPE" <%s>\n",
			    chnl->socketId,error.text);
			removeClientSessionForChannel(chnl);
		}
		else 
		{
		  	switch (retval)
			{
			case RSSL_RET_CHAN_INIT_IN_PROGRESS:
				if (inProg.flags & RSSL_IP_FD_CHANGE)
				{
					printf("\nChannel In Progress - New FD: "SOCKET_PRINT_TYPE"  Old FD: "SOCKET_PRINT_TYPE"\n",chnl->socketId, inProg.oldSocket );

					FD_CLR(inProg.oldSocket,&readfds);
					FD_CLR(inProg.oldSocket,&exceptfds);
					FD_SET(chnl->socketId,&readfds);
					FD_SET(chnl->socketId,&exceptfds);
				}
				else
				{
					printf("\nChannel "SOCKET_PRINT_TYPE" connection in progress\n", chnl->socketId);
				}
				break;

			case RSSL_RET_SUCCESS:
				{
					RsslChannelInfo chanInfo;
					printf("\nClient Channel fd="SOCKET_PRINT_TYPE" is now ACTIVE\n" ,chnl->socketId);
					/* WINDOWS: change size of send/receive buffer since it's small by default */
#ifdef _WIN32
					if (rsslIoctl(chnl, RSSL_SYSTEM_WRITE_BUFFERS, &sendBfrSize, &error) != RSSL_RET_SUCCESS)
					{
						printf("rsslIoctl(): failed <%s>\n", error.text);
					}
					if (rsslIoctl(chnl, RSSL_SYSTEM_READ_BUFFERS, &rcvBfrSize, &error) != RSSL_RET_SUCCESS)
					{
						printf("rsslIoctl(): failed <%s>\n", error.text);
					}
#endif

					/* if device we connect to supports connected component versioning, 
					 * also display the product version of what this connection is to */
					if ((retval = rsslGetChannelInfo(chnl, &chanInfo, &error)) >= RSSL_RET_SUCCESS)
					{
						RsslUInt32 i;
						for (i = 0; i < chanInfo.componentInfoCount; i++)
						{
							printf("Connection is from %s device.\n", chanInfo.componentInfo[i]->componentVersion.data);
						}
					}
				}
				break;

			default:
				printf("\nBad return value fd="SOCKET_PRINT_TYPE" <%s>\n",
				       chnl->socketId,error.text);
				removeClientSessionForChannel(chnl);
				break;
			}
		}
	}
	if(chnl->socketId != -1 && chnl->state == RSSL_CH_STATE_ACTIVE)
	{
		readret = 1;
		while (readret > 0) /* read until no more to read */
		{
			if ((msgBuf = rsslReadEx(chnl, &readInArgs, &readOutArgs, &readret,&error)) != 0)
			{
				if (processRequest(chnl, msgBuf) == RSSL_RET_SUCCESS)
				{
					/* set flag for client message received */
					setReceivedClientMsgForChannel(chnl);
				}

				if (showTransportDetails == RSSL_FALSE)
					printf("\nCompression Stats Bytes In: %d Uncompressed Bytes In: %d\n", readOutArgs.bytesRead, readOutArgs.uncompressedBytesRead);
			}
			if (msgBuf == 0)
			{
				switch (readret)
				{
					case RSSL_RET_CONGESTION_DETECTED:
					case RSSL_RET_SLOW_READER:
					case RSSL_RET_PACKET_GAP_DETECTED:
						if (chnl->state != RSSL_CH_STATE_CLOSED)
						{
							/* disconnectOnGaps must be false.  Connection is not closed */
							printf("\nRead Error: %s <%d>\n", error.text, readret);
							/* break out of switch */
							break;
						}
						/* if channel is closed, we want to fall through */
					case RSSL_RET_FAILURE:
					{
						printf("\nchannelInactive fd="SOCKET_PRINT_TYPE" <%s>\n",
					    chnl->socketId,error.text);
						removeClientSessionForChannel(chnl);
						closeItemChnlStreams(chnl);
						closeDictionaryChnlStreams(chnl);
						closeSrcDirectoryChnlStream(chnl);
						closeLoginChnlStream(chnl);
					}
					break;
					case RSSL_RET_READ_FD_CHANGE:
					{
						printf("\nrsslRead() FD Change - Old FD: "SOCKET_PRINT_TYPE" New FD: "SOCKET_PRINT_TYPE"\n", chnl->oldSocketId, chnl->socketId);
						FD_CLR(chnl->oldSocketId, &readfds);
						FD_CLR(chnl->oldSocketId, &exceptfds);
						FD_SET(chnl->socketId, &readfds);
						FD_SET(chnl->socketId, &exceptfds);
					}
					break;
					case RSSL_RET_READ_PING: 
					{
						/* set flag for client message received */
						setReceivedClientMsgForChannel(chnl);
					}
					break;
					default:
						if (readret < 0 && readret != RSSL_RET_READ_WOULD_BLOCK)
						{
							printf("\nRead Error: %s <%d>\n", error.text, readret);
						
						}
					break;
				}
			}
		}
	}
	else if (chnl->state == RSSL_CH_STATE_CLOSED)
	{
		printf("Channel fd="SOCKET_PRINT_TYPE" Closed.\n", chnl->socketId);
		removeClientSessionForChannel(chnl);
	}
}

/*
 * Binds RSSL server and returns the server to the caller.
 * portno - The port number of the server
 * error - The error information in case of failure
 */
static RsslServer* bindRsslServer(char* portno, RsslError* error)
{
	RsslServer* srvr;
	RsslBindOptions sopts = RSSL_INIT_BIND_OPTS;
	
	sopts.guaranteedOutputBuffers = 500;
	sopts.serviceName = portno;
	sopts.majorVersion = RSSL_RWF_MAJOR_VERSION;
	sopts.minorVersion = RSSL_RWF_MINOR_VERSION;
	sopts.protocolType = RSSL_RWF_PROTOCOL_TYPE;

	if ((srvr = rsslBind(&sopts, error)) != 0)
	{
		printf("\nServer IPC descriptor = "SOCKET_PRINT_TYPE" bound on port %d\n", srvr->socketId, srvr->portNumber);
		FD_SET(srvr->socketId,&readfds);
		FD_SET(srvr->socketId,&exceptfds);
	}

	return srvr;
}

/*
 * Creates a new client session
 * srvr - The server to create session for
 */
static void createNewClientSession(RsslServer *srvr)
{
	int i;
	RsslRet ret;
	RsslChannel *sckt;
	RsslError	error;
	RsslBool	clientSessionFound = RSSL_FALSE;
	RsslAcceptOptions acceptOpts = RSSL_INIT_ACCEPT_OPTS;

	clientSessionCount++;

	if (clientSessionCount <= NUM_CLIENT_SESSIONS)
	{
		acceptOpts.nakMount = RSSL_FALSE;
	}
	else
	{
		acceptOpts.nakMount = RSSL_TRUE;
	}
	if ((sckt = rsslAccept(srvr, &acceptOpts, &error)) == 0)
	{
		printf("rsslAccept: failed <%s>\n",error.text);
		cleanUpAndExit();
	}
	else 
	{
		if (xmlTrace)
		{
			int debugFlags = 0x2C0;
			RsslTraceOptions traceOptions;

			rsslClearTraceOptions(&traceOptions);
			traceOptions.traceMsgFileName = traceOutputFile;
			traceOptions.traceMsgMaxFileSize = 10000000;
			traceOptions.traceFlags |= RSSL_TRACE_TO_FILE_ENABLE | RSSL_TRACE_TO_MULTIPLE_FILES | RSSL_TRACE_TO_STDOUT | RSSL_TRACE_WRITE | RSSL_TRACE_READ;
			rsslIoctl(sckt, (RsslIoctlCodes)RSSL_TRACE, (void *)&traceOptions, &error);
		}
		/* find an available client session */
		for (i = 0; i < CLIENT_SESSIONS_LIMIT; i++)
		{
			if (clientSessions[i].clientChannel == NULL)
			{
				clientSessions[i].clientChannel = sckt;
				clientSessionFound = RSSL_TRUE;
				break;
			}
		}

		/* close channel if no more client sessions */
		if (clientSessionFound == RSSL_FALSE &&
			clientSessionCount > CLIENT_SESSIONS_LIMIT)
		{
			clientSessionCount--;
			if ((ret = rsslCloseChannel(sckt, &error)) < RSSL_RET_SUCCESS)
			{
				printf("rsslCloseChannel() failed with return code: %d\n", ret);
			}
		}
		else
		{
			printf("\nServer fd="SOCKET_PRINT_TYPE": New client on Channel fd="SOCKET_PRINT_TYPE"\n",
				srvr->socketId,sckt->socketId);
			FD_SET(sckt->socketId,&readfds);
			FD_SET(sckt->socketId,&exceptfds);
		}
	}
}

/*
 * Initializes the ping times for a client session.
 * clientSessionInfo - The client session to initialize ping times for
 */
static void initPingTimes(RsslClientSessionInfo* clientSessionInfo)
{
	time_t currentTime = 0;

	/* get current time */
	time(&currentTime);

	/* set time to send next ping from server */
	clientSessionInfo->nextSendPingTime = currentTime + (time_t)(clientSessionInfo->clientChannel->pingTimeout/3);

	/* set time server should receive next message/ping from client */
	clientSessionInfo->nextReceivePingTime = currentTime + (time_t)(clientSessionInfo->clientChannel->pingTimeout);

	clientSessionInfo->pingsInitialized = RSSL_TRUE;
}

/*
 * Initializes the run-time for the rsslProvider.
 */
static void initRuntime()
{
	time_t currentTime = 0;

	/* get current time */
	time(&currentTime);
	
	rsslProviderRuntime = currentTime + (time_t)timeToRun;
}

/*
 * Handles the ping processing for all active client sessions.
 * Sends a ping to the client if the next send ping time has
 * arrived and checks if a ping has been received from a client
 * within the next receive ping time.
 */
static void handlePings()
{
	int i;
	time_t currentTime = 0;

	/* get current time */
	time(&currentTime);

	for (i = 0; i < NUM_CLIENT_SESSIONS; i++)
	{
		if ((clientSessions[i].clientChannel != NULL) && (clientSessions[i].clientChannel->socketId != -1))
		{
			/* handle sending pings to clients */
			if ((clientSessions[i].pingsInitialized == RSSL_TRUE) && (currentTime >= clientSessions[i].nextSendPingTime))
			{
				/* send ping to client */
				if (sendPing(clientSessions[i].clientChannel) != RSSL_RET_SUCCESS)
				{
					cleanUpAndExit();
				}

				/* set time to send next ping to client */
				clientSessions[i].nextSendPingTime = currentTime + (time_t)(clientSessions[i].clientChannel->pingTimeout/3);
			}

			/* handle receiving pings from client */
			if ((clientSessions[i].pingsInitialized == RSSL_TRUE) && (currentTime >= clientSessions[i].nextReceivePingTime))
			{
				/* check if server received message from client since last time */
				if (clientSessions[i].receivedClientMsg)
				{
					/* reset flag for client message received */
					clientSessions[i].receivedClientMsg = RSSL_FALSE;

					/* set time server should receive next message/ping from client */
					clientSessions[i].nextReceivePingTime = currentTime + (time_t)(clientSessions[i].clientChannel->pingTimeout);
				}
				else /* lost contact with client */
				{
					printf("\nLost contact with client fd="SOCKET_PRINT_TYPE"\n", clientSessions[i].clientChannel->socketId);
					removeClientSession(&clientSessions[i]);
				}
			}
		}
	}
}

/*
 * Handles the run-time for the rsslProvider.  Sends close status
 * messages to all streams on all channels after run-time has elapsed.
 */
static void handleRuntime()
{
	int i;
	time_t currentTime = 0;
	RsslRet	retval = 0;
	RsslError error;

	/* get current time */
	time(&currentTime);

	if (currentTime >= rsslProviderRuntime)
	{
		/* send close status messages to all streams on all channels */
		for (i = 0; i < NUM_CLIENT_SESSIONS; i++)
		{
			if ((clientSessions[i].clientChannel != NULL) && (clientSessions[i].clientChannel->socketId != -1))
			{
				/* send close status messages to all item streams */
				//sendItemCloseStatusMsgs(clientSessions[i].clientChannel);
				

				/* send close status messages to dictionary streams */
				sendDictionaryCloseStatusMsgs(clientSessions[i].clientChannel);

				/* flush before exiting */
				if (FD_ISSET(clientSessions[i].clientChannel->socketId, &wrtfds))
				{
					retval = 1;
					while (retval > RSSL_RET_SUCCESS)
					{
						retval = rsslFlush(clientSessions[i].clientChannel, &error);
					}
					if (retval < RSSL_RET_SUCCESS)
					{
						printf("rsslFlush() failed with return code %d - <%s>\n", retval, error.text);
					}
				}
			}
		}

		printf("\nrsslProvider run-time expired...\n");
		cleanUpAndExit();
	}
}

/*
 * Processes a request from a channel.  This consists of
 * performing a high level decode of the message and then
 * calling the applicable specific function for further
 * processing.
 * chnl - The channel of the request
 * buffer - The message buffer containing the request
 */
static RsslRet processRequest(RsslChannel* chnl, RsslBuffer* buffer)
{
	RsslRet ret = 0;
	RsslMsg msg = RSSL_INIT_MSG;
	RsslDecodeIterator dIter;
	
	/* clear decode iterator */
	rsslClearDecodeIterator(&dIter);
	
	/* set version info */
	rsslSetDecodeIteratorRWFVersion(&dIter, chnl->majorVersion, chnl->minorVersion);

	rsslSetDecodeIteratorBuffer(&dIter, buffer);

	ret = rsslDecodeMsg(&dIter, &msg);				
	if (ret != RSSL_RET_SUCCESS)
	{
		printf("\nrsslDecodeMsg(): Error %d on SessionData fd="SOCKET_PRINT_TYPE"  Size %d \n", ret, chnl->socketId, buffer->length);
		removeClientSessionForChannel(chnl);
		return RSSL_RET_FAILURE;
	}

	switch ( msg.msgBase.domainType )
	{
		case RSSL_DMT_LOGIN:
			if (processLoginRequest(chnl, &msg, &dIter) != RSSL_RET_SUCCESS)
			{
				removeClientSessionForChannel(chnl);
				return RSSL_RET_FAILURE;
			}
			if (!isDictionaryReady())
			{
				 RsslLoginRequestInfo* loginReqInfo = findLoginReqInfo(chnl);
				 if (loginReqInfo->SupportProviderDictionaryDownload)
				 {
					sendDictionaryRequests(chnl);
					printf("Send Dictionary Request\n");
				 }
				 else
				 {
					printf("\nDictionary could not be downloaded, the connection does not support Provider Dictionary Download\n");
					cleanUpAndExit();
				 }
			}

			break;
		case RSSL_DMT_SOURCE:
			if (processSourceDirectoryRequest(chnl, &msg, &dIter) != RSSL_RET_SUCCESS)
			{
				removeClientSessionForChannel(chnl);
				return RSSL_RET_FAILURE;
			}
			break;
		case RSSL_DMT_DICTIONARY:
			if (msg.msgBase.msgClass == RSSL_MC_REQUEST)
			{
				if (isDictionaryReady())
				{
			if (processDictionaryRequest(chnl, &msg, &dIter) != RSSL_RET_SUCCESS)
			{
				removeClientSessionForChannel(chnl);
				return RSSL_RET_FAILURE;
			}
				}
				else
				{
					removeClientSessionForChannel(chnl);
					return RSSL_RET_FAILURE;
				}
			}
			else
			{
				if (processDictionaryResponse(chnl, &msg, &dIter) != RSSL_RET_SUCCESS)
				{
					removeClientSessionForChannel(chnl);
					return RSSL_RET_FAILURE;
				}
			}
			break;
		case RSSL_DMT_MARKET_PRICE:
		case RSSL_DMT_MARKET_BY_ORDER:
		case RSSL_DMT_SYMBOL_LIST:
		case RSSL_DMT_MARKET_BY_PRICE:
		case RSSL_DMT_YIELD_CURVE:
			if (isDictionaryReady() && processItemRequest(chnl, &msg, &dIter) != RSSL_RET_SUCCESS)
			{
				removeClientSessionForChannel(chnl);
				return RSSL_RET_FAILURE;
			}
			break;
		default:
			switch(msg.msgBase.msgClass)                                                                                                                                                                    
			{
				case RSSL_MC_REQUEST:                                                                                                                                                                             
					if (sendNotSupportedStatus(chnl, &msg) != RSSL_RET_SUCCESS)
					{                                                                                                                                                                                             
						removeClientSessionForChannel(chnl);
						return RSSL_RET_FAILURE;
					}                                                                                                                                                                                             
					break;                                                                                                                                                                                        

				case RSSL_MC_CLOSE:
					printf("Received Close message with StreamId %d and unsupported domain %u\n\n",
							msg.msgBase.streamId, msg.msgBase.domainType);
					break;

				default:
					printf("Received unhandled message with MsgClass %u, StreamId %d and unsupported domain %u\n\n",
							msg.msgBase.msgClass, msg.msgBase.streamId, msg.msgBase.domainType);
					break;
			}
			break;
	}

	return RSSL_RET_SUCCESS;
}

/*
 * Removes a client session.
 * clientSessionInfo - The client session to be removed
 */
static void removeClientSession(RsslClientSessionInfo* clientSessionInfo)
{
	clientSessionCount--;
	removeChannel(clientSessionInfo->clientChannel);
	clearClientSessionInfo(clientSessionInfo);
}

/*
 * Removes a channel.
 * chnl - The channel to be removed
 */
static void removeChannel(RsslChannel* chnl)
{
	RsslError error;
	RsslRet ret;

	FD_CLR(chnl->socketId, &readfds);
	FD_CLR(chnl->socketId, &exceptfds);
	if (FD_ISSET(chnl->socketId, &wrtfds))
		FD_CLR(chnl->socketId, &wrtfds);

	if ((ret = rsslCloseChannel(chnl, &error)) < RSSL_RET_SUCCESS)
	{
		printf("rsslCloseChannel() failed with return code: %d\n", ret);
	}
}

/*
 * Sets the ReceivedClientMsg flag for a channel.
 * chnl - The channel to set the ReceivedClientMsg flag for
 */
static void setReceivedClientMsgForChannel(RsslChannel *chnl)
{
	int i;

	for (i = 0; i < NUM_CLIENT_SESSIONS; i++)
	{
		if (clientSessions[i].clientChannel == chnl)
		{
			clientSessions[i].receivedClientMsg = RSSL_TRUE;
			break;
		}
	}
}

/*
 * Removes a client session for a channel.
 * chnl - The channel to remove the client session for
 */
static void removeClientSessionForChannel(RsslChannel *chnl)
{
	int i;

	for (i = 0; i < CLIENT_SESSIONS_LIMIT; i++)
	{
		if (clientSessions[i].clientChannel == chnl)
		{
			removeClientSession(&clientSessions[i]);
			break;
		}
	}
}

/*
 * Cleans up and exits the application.
 */
void cleanUpAndExit()
{
	int i;

	/* clean up client sessions */
	for (i = 0; i < NUM_CLIENT_SESSIONS; i++)
	{
		if ((clientSessions[i].clientChannel != NULL) && (clientSessions[i].clientChannel->socketId != -1))
		{
			removeChannel(clientSessions[i].clientChannel);
		}
	}

	/* clean up server */
	FD_CLR(rsslSrvr->socketId, &readfds);
	FD_CLR(rsslSrvr->socketId, &exceptfds);
	rsslCloseServer(rsslSrvr, &error);

	/* free memory for dictionary */
	freeDictionary();

	rsslUninitialize();

	/* WINDOWS: wait for user to enter something before exiting  */
#ifdef _WIN32
	printf("\nPress Enter or Return key to exit application:");
	getchar();
#endif
	exit(RSSL_RET_FAILURE);
}


