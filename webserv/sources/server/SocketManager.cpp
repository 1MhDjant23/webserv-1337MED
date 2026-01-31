#include "../../includes/serverHeader/Server.hpp"
#include "../../includes/serverHeader/SocketManager.hpp"
#include "../../includes/serverHeader/ServerUtils.hpp"
#include "../../includes/serverHeader/Client.hpp"
#include "../../includes/serverHeader/CookiesSessionManager.hpp"
#include "../../includes/response.hpp"

bool    g_run = true;

void    signalHandler(int sig) {
    (void) sig;
    std::cout << "\nClosing server..." << std::endl;
	g_run = false;
    return;
}

void    SocketManager::hanldVirtualHost( std::vector<TableOfListen>& tableOfListen ) {
    /**
     * just i set the apropriate state for each Listen directives in .conf file;
     * that will help to bind all Listen & prevent duplicate binding
    */

    for (size_t i = 0; i < tableOfListen.size(); i++)
    {
        for (size_t k = i; k < tableOfListen.size(); k++)
        {
            if (i == k || tableOfListen[i]._interfaceState.alreadyBinded == true)
                continue ; /* prevent check the same table */
            if (tableOfListen[i] == tableOfListen[k])
                tableOfListen[k]._interfaceState.alreadyBinded = true;
        }
    }
}

SocketManager::SocketManager(Data& config, std::vector<TableOfListen>& tableOfListen) : _config(&config), _tableOfListen(tableOfListen) {
    g_console.log(SOCKET_MANAGER, str("Start managing listening sockets..."), BG_GREEN);
}

void    SocketManager::setTableOfListen(std::vector<TableOfListen>& table) {
    std::set<std::pair<str, str> >::iterator    it;

    for (size_t serverId = 0; serverId < _config->_servers.size(); serverId++)
    {
        it = _config->_servers[serverId]._listen.begin();
        while (it != _config->_servers[serverId]._listen.end())
        {
            TableOfListen  tmp;
            tmp._fd = -1;  
            tmp.addr_len = 0;
            tmp._ip = it->first;
            tmp._port = it->second;
            tmp._serverName = _config->_servers[serverId]._serverName;
            tmp._serverBlockId = serverId; /* on each server block this ip:port come */
            tmp._interfaceState.alreadyBinded = false;
            tmp._interfaceState.fd = -1;
            table.push_back(tmp);
            it++;
        }
    }
}

void    SocketManager::initSockets(void) {
    struct  addrinfo    hints, *results;
    int status;

    std::memset(&hints, 0, sizeof(hints));
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; //tell getaddrinfo() that the returned address will be used
    //               for binding a server socket, not for connecting as a client.
    for (size_t counter = 0; counter < _tableOfListen.size(); counter++)
    {
        if (!_tableOfListen[counter]._interfaceState.alreadyBinded)
        {
            if ((status = getaddrinfo(_tableOfListen[counter]._ip.c_str(), _tableOfListen[counter]._port.c_str(), &hints, &results)) != 0)
                throw std::runtime_error(gai_strerror(errno));
            int fd = socket(results->ai_family, results->ai_socktype, IPPROTO_TCP);
            if (fd == -1) {
                closeListenSockets();
                throw std::runtime_error(strerror(errno));
            }
            SocketManager::setNonBlocking(fd);
            _tableOfListen[counter]._fd = fd;
            /* set same SOCKET_FD to each identique IP:PORT  */
            for (size_t S = 0; S < _tableOfListen.size(); S++)
            {
                if (_tableOfListen[S]._interfaceState.alreadyBinded && _tableOfListen[S] == _tableOfListen[counter])
                {
                    _tableOfListen[S]._fd = fd;
                }
            }
            /* set option for that socket */
            int addrYes = 1;
            setsockopt(_tableOfListen[counter]._fd, SOL_SOCKET, SO_REUSEADDR, &addrYes, sizeof(addrYes));
            std::memcpy(&_tableOfListen[counter].addr, results->ai_addr, results->ai_addrlen);
            _tableOfListen[counter].addr_len = results->ai_addrlen;
            freeaddrinfo(results);
            bindSockets(counter);
        }
    }
}



void    SocketManager::setListenEvent(std::vector<struct pollfd>& _pollfd) {
    size_t  count = 0;

    for (size_t i = 0; i < _tableOfListen.size(); i++)
    {
        if (!_tableOfListen[i]._interfaceState.alreadyBinded)
        {
            _pollfd[count].fd = _tableOfListen[i]._fd; // fill "struct pollfd" with listen socket FDs
            _pollfd[count].events = POLLIN;
            count++;
        }
    }
}

void    SocketManager::checkForNewClients( std::vector<struct pollfd>& _pollfd, Server& _server ) {
    int 	clientFd;

    for (size_t	i = 0; i < portCounter(); i++)
    {
        if (_pollfd[i].revents & POLLIN)
        {
            if ((clientFd = accept(_pollfd[i].fd, NULL, NULL)) == -1)
            {
                closeListenSockets();
                _server.closeClientConnection();
                throw std::runtime_error(strerror(errno));
            }
            #ifdef SO_NOSIGPIPE // to prevent compilation error on linux, 'SO_NOSIGPIPE' is a macOS only
            int sigYes = 1;
            setsockopt(clientFd, SOL_SOCKET, SO_NOSIGPIPE, &sigYes, sizeof(sigYes));
            #endif
            _server.addClients(Client(clientFd, detectServerBlock(_pollfd[i].fd)), _pollfd);
            SocketManager::setNonBlocking(clientFd);
            std::cout << CONNECTION << YELLOW << "New Client Connected, SOCKET_FD=" << clientFd << RESET << std::endl;
        }
    }
}

Status  SocketManager::PollingForEvents(std::vector<struct pollfd>& pollFd, Server& server,size_t cltSize) {
    int 	        totalEvent;
    wsrv_timer_t    coreTimeOut;

    (cltSize == 0 ? coreTimeOut = -1 : coreTimeOut = server.wsrv_find_next_timeout()*1000);
    displayPOllList(pollFd);
    g_console.log(SERVER, str("POLLING For Events..."), BG_GREEN);
    totalEvent = poll(pollFd.data(), pollFd.size(), static_cast<int>(coreTimeOut));
    if (totalEvent == -1)
    {
        std::cerr << RED << "Poll: " << strerror(errno) << RESET << std::endl;
        return NON;
    }
    server.wsrv_timeout_closer(pollFd); /* remove client that timed-out  */
    return NON;
}

void    SocketManager::handlErrCloses( std::vector<struct pollfd>& _pollfd, Server& server ){
    size_t  clientStart = portCounter();

    for (size_t i = clientStart; i < server.getListOfClients().size(); i++)
    {
        if ( _pollfd[i].revents & ( POLLHUP | POLLERR | POLLNVAL ) )
        {
            std::cout << WARNING << RED << "Client FD=" << _pollfd[i].fd << " Closed Connection Unexpectedly" << RESET << std::endl;
            server.handleDisconnect(i - portCounter(), _pollfd);
            i--;
        }
    }
}

void    SocketManager::runCoreLoop(void) {
    size_t                      cltStart = portCounter();
    CookiesSessionManager       sessionManager;
    Server                      _server(sessionManager, portCounter());
    std::vector<struct pollfd>  _pollfd(portCounter());

    setListenEvent(_pollfd);    // set listen socket to wait for POLLIN events
    std::vector<Client>&    _clients = _server.getListOfClients(); 
    signal(SIGPIPE, SIG_IGN);
    while (g_run)
    {
        // sessionManager.displayAllSession();  /* displaying All session saved in server */
        /*
         ** Main loop events:
         ** polling for incoming event (POLLIN & POLLOUT || ( POLLHUP | POLLERR | POLLNVAL ))
        */
        PollingForEvents(_pollfd, _server, _clients.size());
        /**
         *  handle close/error from client-side. 
         *  if one of these events ( POLLHUP | POLLERR | POLLNVAL ) occure.
         * */
        handlErrCloses( _pollfd, _server );
/*++++++++++++++++++  Check Listen Sockets For new Connect: ++++++++++++++++++*/ 
        /**
         * Loop over all listening socket for incoming new Client,
         * accept & add him to the pollfd{}
        */
        checkForNewClients(_pollfd, _server);
/*
 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 ++++++++++++++++++            CGI Events Part:            ++++++++++++++++++
 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
*/ 
        if (portCounter() + _clients.size() < _pollfd.size()) { /* ensure there is at least one cgi Running (pipe fd) */
            /**
             * loop over all opened pipe and check if there is some data to read.
             * 
            */
            cgiEventsChecking(_clients, _pollfd, _server);
        }
/*
 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 ++++++++++++++++++    Requests & Responses  From Users:   ++++++++++++++++++
 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ 
*/
        // size_t  cltScop = portCounter() + _clients.size();
        for (size_t i = cltStart; i < ( portCounter() + _clients.size() ); i++) {

            /*     ***************     Request Part    ***************    */
			
            if ( _pollfd[i].revents & POLLIN ) {
                ClientState state = _server.readRequest(i - cltStart);
                _clients[i - cltStart].setStartTime(std::time(NULL)); /* reset time-out */
                if (state == CS_READING_DONE)
                {
                    _pollfd[i].events |= POLLOUT;
                    _pollfd[i].events &= ~POLLIN;
                }
                else if (state == CS_DISCONNECT || state == CS_FATAL)
                {
                    std::cout << NOTICE << RED << "SOCKET_FD=" << _pollfd[i].fd << ", peer closed connection" << RESET << std::endl;
                    _server.handleDisconnect(i - cltStart, _pollfd);
                    i--;
                    continue;
                }
                else if (state == CS_READING) /* still reading request ...*/
                    continue;
            }
            /*    ***************      Response Part     ***************    */
            if ( _pollfd[i].revents & POLLOUT )
            {
                /*
                 * If User req CGI, Run CGI Script and wait for results next polling!
                 * ```!_clients[i-cltStart]._alreadyExec``` == Script Of CGI Already Running.
                 * So prevent to run it multiple-time!
                 */
                Client&	client = _server.getListOfClients()[i - cltStart];
                client.setStartTime(std::time(NULL));                           /* reset time-out */
                if (client.getStatus() == CS_CGI_REQ) {
                    Response response;
                /* ****** CGI Handler exec/response ****** */
                    if (!client._alreadyExec)                                   /* run CGI-script one time per-request & add CGIproc{pipe-fd to pollfd, pid} to client-data */
                    {
                        client.setTimeOut(getSrvBlock( client._serverBlockHint, client.getRequest())->_cgiTimeout); /* set time-out for cgi-script */
                        if (isCgiRequest(_pollfd, _clients[i - cltStart], i)) {
                            client.setStartTime(std::time(NULL));                           /* reset time-out */
                            if (client.getCltCgiState() == CCS_FAILLED) {
                                if (CGI_errorResponse(_clients[i - cltStart], _clients[i - cltStart]._cgiProc._statusCode) == CLOSED) {
                                    _server.handleDisconnect(i - cltStart, _pollfd);
                                    i--;
                                    continue;
                                }
                                client.setTimeOut(getSrvBlock( client._serverBlockHint, client.getRequest())->_headerTimeout); /* waiting for new request */
                                client.setClientState(CS_NEW);
                                _pollfd[i].events |= POLLIN;
                                _pollfd[i].events &= ~POLLOUT;
                            }
                            continue;
                        }
                    }
                    else if (client.getCltCgiState() == CCS_DONE) /** when CGI-script Done, we send appropriate response here. */
                    {
                        client.setStartTime(std::time(NULL));                           /* reset time-out */
                        if (client._cgiOut._output.empty())
                        {
                            /* nothing to send  */
                            _pollfd[i].events |= POLLIN;
                            _pollfd[i].events &= ~POLLOUT;
                            client.setClientState(CS_NEW);
                            client._alreadyExec = false;
                            client.setResponse(response);
                            client.setTimeOut(getSrvBlock( client._serverBlockHint, client.getRequest())->_headerTimeout); /* reset time from cgi-time to header-time */
                            continue;
                        }
                        size_t  toSend = std::min<size_t>(client._cgiOut._output.size(), CGI_SEND_BUFFER);
                        ssize_t sendByte = send(client.getFd(), client._cgiOut._output.c_str(), toSend, 0);
                        if (sendByte > 0)
                        {
                            client._cgiOut._output.erase(client._cgiOut._output.begin(), client._cgiOut._output.begin() + sendByte);
                            continue;
                        }
                        if (sendByte == 0 ) {
                            _server.handleDisconnect(i - cltStart, _pollfd);
                            i--;
                            continue;
                        }
                        if (sendByte < 0)
                            continue ;                          /* will check errors in `handlErrCloses()` */
                    }
                } 
    /* **************************   Response for static-files    **************************************** */
                if (client.getStatus() != CS_CGI_REQ && client._sendInfo.resStatus != CS_WRITING_DONE)  /** Handle response for normal HTTP request */
				{
					sendResponse(client, sessionManager);
					size_t	dataLen = client._sendInfo.buff.size();
					const char* dataPtr = client._sendInfo.buff.data();

					ssize_t byte = send(_pollfd[i].fd, dataPtr, dataLen, 0);
                    client.setStartTime(std::time(NULL)); /* reset time-out */
					if (byte == 0)
					{
						_server.handleDisconnect(i - cltStart, _pollfd);
                        i--;
						continue;
					}
					else if (byte < 0)
                        continue;
					else if (byte > 0)
						client._sendInfo.buff.erase(client._sendInfo.buff.begin(), client._sendInfo.buff.begin() + byte);
				}
				if (client._sendInfo.resStatus == CS_WRITING_DONE)
				{
                    if (client._sendInfo.connectionState == CLOSED) {
                        _server.handleDisconnect(i - cltStart, _pollfd);
                        i--;
                        continue;
                    }
                    if (client._sendInfo.fd != -1)
                    {
                        close(client._sendInfo.fd);
                        client._sendInfo.connectionState = NEW;
                        client._sendInfo.fd = -1;
                    }
                    client._sendInfo.buff.clear();
                    client.setClientState(CS_NEW);
                    client._reqInfo.reqStatus = CS_NEW;
					_pollfd[i].events |= POLLIN;
					_pollfd[i].events &= ~POLLOUT;
				}
            }
        }
	}
    closeClientsSockets(_clients);
    closeListenSockets();
}

bool    SocketManager::isCgiRequest(std::vector<struct pollfd>& pollFd, Client& client, size_t index)
{
    CGIProc proc;

    if (!client._alreadyExec)
    {
        proc = cgiHandle(client.getCgiContext(), &client._alreadyExec);
        if (proc._error)
        {
            std::cout << WARNING << BG_RED << "Unexpected Error On CGI"  << ", Client FD=" << client.getFd() << RESET << std::endl;
            client._cgiProc = proc;
            client._alreadyExec = false; /* reset to default state (script not executed yet) */
            client.setCltCgiState(CCS_FAILLED);
            return true;
        }
        struct  pollfd  pollPipe;
        pollPipe.fd = proc._readPipe;
        pollPipe.events = POLLIN;
        pollPipe.revents = 0;
        pollFd.push_back(pollPipe);
        client._cgiProc = proc;
        pollFd[index].events &= ~POLLOUT;
        client.setCltCgiState(CCS_RUNNING);
        return true;
    }
    return  false;
}

void    SocketManager::cgiEventsChecking(std::vector<Client>& clients, std::vector<struct pollfd>& pollFd, Server& srvr){
    size_t  cgiScop = portCounter() + clients.size();

    for (size_t i = cgiScop; i < pollFd.size(); i++)
    {
        if ( pollFd[i].revents & ( POLLIN | POLLHUP ) )
        {
            // std::cout << "Data To Read From CGI PIPE=" << pollFd[i].fd << std::endl;
            /**
             * read result from pipe;
             */
            Client& clinet = srvr.getClientReqCGI(pollFd[i].fd);
            if (pollFd[i].fd == clinet._cgiProc._readPipe)
            {
                readChild(clinet);
                /**
                 * check if Done to set The Client to POLLOUT & remove pipe from pollfd struct.
                 */
                if (clinet.getCltCgiState() == CCS_DONE || clinet.getCltCgiState() == CCS_FAILLED)
                {
                    for (size_t i = portCounter(); i < portCounter() + clients.size(); i++)
                    {
                        if (clinet.getFd() == pollFd[i].fd)
                        {
                            pollFd[i].events |= POLLOUT;
                            pollFd[i].events &= ~POLLIN;
                        }
                    }
                    if (clinet._cgiProc._readPipe != -1) {
                        close(clinet._cgiProc._readPipe);
                        clinet._cgiProc._readPipe = -1;
                    }
                    pollFd.erase(pollFd.begin() + i); /* remove pipe fd from pollfd{} */
                    generate_CGI_Response(clinet); // generate headers for CGI
                    continue;
                }
            }
            else
                continue;
        }
    }
}

serverBlockHint    SocketManager::detectServerBlock(int sockFd) const {
    serverBlockHint   tmp;
    for (size_t i = 0; i < _tableOfListen.size(); i++)
    {
        if (sockFd == _tableOfListen[i]._fd)
        {
            tmp.push_back(std::make_pair(&_tableOfListen[i],  &(_config->_servers[_tableOfListen[i]._serverBlockId])));
        }
    }
    return tmp;
}

void    SocketManager::closeListenSockets(void) const {
    for (size_t i = 0; i < _tableOfListen.size(); i++)
    {
        if (!_tableOfListen[i]._interfaceState.alreadyBinded)
            close(_tableOfListen[i]._fd);
    }
    g_console.log(DISCONNECTION, "Listening Sockets Closed", RED);
}

size_t SocketManager::portCounter(void) const {
    size_t count = 0;
    for (size_t i = 0; i < _tableOfListen.size(); i++)
    {
        if (!_tableOfListen[i]._interfaceState.alreadyBinded)
            count++;
    }
    return count;
}

void    SocketManager::closeClientsSockets(std::vector<Client>& clients) {
    for (size_t i = 0; i < clients.size(); i++)
    {
        if (clients[i]._cgiProc._readPipe != -1)
            close(clients[i]._cgiProc._readPipe);
        if (clients[i]._cgiProc._childPid != -1)
        {
            kill(clients[i]._cgiProc._childPid, SIGKILL);
            int status;
            waitpid(clients[i]._cgiProc._childPid, &status, 0);
        }
        if (clients[i]._sendInfo.fd != -1)
            close(clients[i]._sendInfo.fd);
        if (clients[i]._sendInfo.buff.size() != 0)
            clients[i]._sendInfo.buff.clear();
        if (clients[i]._uploadFd != -1) /* Close Upload Fd if exist */
            close(clients[i]._uploadFd);
        if (clients[i]._reqInfo.buffer.size() != 0)
            clients[i]._reqInfo.buffer.clear();
        close(clients[i].getFd());
    }
    std::cout << INFO << GREEN << "Clients Resources are Cleanned" << RESET << std::endl;
}
