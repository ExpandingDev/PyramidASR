#include <iostream>
#include <fstream>
#include <memory>

///TODO: Find Windows replacement method for Daemonizing and unix sockets
#include <ctype.h>
#include <cstring>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>

#include <syslog.h>
#include <dbus-cxx.h>
#include <glib.h>

#include "config.h"
#include "ASRServiceAdapter.h"
#include "PyramidASRService.h"

#define LOCK_FILE "pyramid.lock"

PyramidASRService * service;

pid_t PID;

bool customAddressSet;
char CUSTOM_ADDRESS[200];

void signalHandler(int);
void daemonize();
void registerSignalHandles();
void doLoop();

char RUNNING_DIR[200] = DEFAULT_RUNNING_DIRECTORY;

void registerSignalHandles() {
    //The below signal handler is commented out because dbus-cxx has its own SIGCHLD handler, so setting it to ignore causes issues.
	//signal(SIGCHLD,SIG_IGN); // ignore child
	signal(SIGTSTP,signalHandler); /* since we haven't daemonized yet, process the TTY signals */
	signal(SIGQUIT,signalHandler);
	signal(SIGHUP,signalHandler);
	signal(SIGTERM,signalHandler);
	signal(SIGPIPE,SIG_IGN); //Ignore bad pipe signals for now
}

void signalHandler(int signal) {
	switch (signal){
		case SIGHUP:
			//syslog(LOG_INFO,"SIGHUP Signal Received...");
			///TODO: Make service reload
			break;
		case SIGQUIT:
		case SIGTSTP:
		case SIGTERM:
			//syslog(LOG_INFO,"Terminate Signal Received...");
			///TODO: Make service stop
			service->running.store(false);
			//exit(0);
			break;
		default:
			//syslog(LOG_INFO, "Received unknown SIGNAL.");
			break;
	}
}

void doLoop() {

    syslog(LOG_DEBUG, "Entered main loop");

    ///Many thanks to the dbus-cxx quickstart guide: https://dbus-cxx.github.io/quick_start_example_0.html#quick_start_server_0_code
    DBus::Dispatcher::pointer dispatcher;
    DBus::Connection::pointer conn;

    try {
        DBus::init();        
        syslog(LOG_DEBUG, "DBus::init called");        
        dispatcher = DBus::Dispatcher::create();        
        syslog(LOG_DEBUG, "dispatcher created");        
        
        //This is used exclusively for when the user specifies a different bus address to connect to.        
        DBusConnection * c;
	   
        if(customAddressSet) {
            syslog(LOG_DEBUG, "Attempting to connect to bus %s", CUSTOM_ADDRESS);  
           
            //Connect to custom bus, send org.freedesktop.DBus.Hello, request name
    		DBusError err; 
    		dbus_error_init(&err);
    
    		c = dbus_connection_open(CUSTOM_ADDRESS, &err);
    		if(dbus_error_is_set(&err)) {
    		    std::cerr << "Failed to connect to address " << CUSTOM_ADDRESS << std::endl;
    			fprintf(stderr, "Connection Error (%s)\n", err.message);
    			dbus_error_free(&err);
    			exit(1);
    		}
    		syslog(LOG_DEBUG, "Connected to bus, attempting to send Hello...");
    		
    		conn = dispatcher->create_connection(c);
    		if(!conn->is_registered()) { // Send org.freedesktop.Hello if we haven't already yet because we started out connection outside of dbus-cxx
    		    conn->bus_register();
    		}
    		syslog(LOG_DEBUG, "Sent hello");
        }
        else { // If no special address is specified, default to the session bus
	        conn = dispatcher->create_connection(DBus::BUS_SESSION);
	        syslog(LOG_DEBUG, "Connected to session bus");
        }
        
        if(NULL == conn) { // Make sure the connection is good
		    std::cerr << "Failed to connect to address " << CUSTOM_ADDRESS << std::endl;
			exit(1);
		}
        
        int ret = conn->request_name("ca.l5.expandingdev.PyramidASR", DBUS_NAME_FLAG_REPLACE_EXISTING);
        if (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER != ret) { // Make sure we are the only instance on the DBus
            std::cerr << "Failed to reserve dbus name! Exiting..." << std::endl;
            syslog(LOG_ERR, "Failed to reserve dbus name! Exiting...");
            exit(-1);
        }

        syslog(LOG_DEBUG, "Reserved DBus name");

        service = new PyramidASRService();

        Buckey::ASRServiceAdapter::pointer a = Buckey::ASRServiceAdapter::create(service, "/ca/l5/expandingdev/PyramidASR");
		if(!conn->register_object(a)) {
			std::cerr << "Failed to register the PyramidASR object!" << std::endl;
			syslog(LOG_ERR, "Failed to register the PyramidASR object onto the DBus!");
		}
		else {
			std::cout << "Registered PyramidASR object" << std::endl;
			syslog(LOG_DEBUG, "Registered the PyramidASR object onto the DBus");
		}
		
		service->setPID(PID);
		service->signalStatus();
		
        while(service->running.load()) {

		}
		delete service;
		
		if(customAddressSet) { // Close the custom DBusConnection once we are done
			dbus_connection_close(c);
		}
    }
    catch (std::shared_ptr<DBus::Error> e) {
        std::cerr << "Caught DBusError: " << e->message() << std::endl;
        syslog(LOG_ERR, "Caught DBusCxx Error: %s", e->message());
        throw e;
    }
}

///A good portion of this code was borrowed from the example code provided at: http://www.enderunix.org/docs/eng/daemon.php
///Which was coded by: Levent Karakas <levent at mektup dot at> May 2001
///Many thank Levent!
void daemonize() {
	int i, lfp;
	char str[10];

	if(getppid() == 1) {
		return; /* already a daemon */
	}

	i=fork();
	if (i<0) {
		exit(1); /* fork error */
	}

	if (i>0) {
		exit(0); /* parent exits */
	}
	/* child (daemon) continues */

	setsid(); /* obtain a new process group */
	for (i=getdtablesize(); i >= 0; --i) {
		close(i); /* close all descriptors */
	}

	/* handle standard I/O */
	i = open("/dev/null",O_RDWR);
	dup(i);
	dup(i);

	umask(027); /* set newly created file permissions */

	lfp=open(LOCK_FILE,O_RDWR|O_CREAT,0640);

	if (lfp<0) {
		exit(1); /* can not open */
	}

	if (lockf(lfp,F_TLOCK,0)<0) {
		syslog(LOG_WARNING, "Unable to start Imitate TTS Service! Lock file is already locked! Make sure that there isn't another Mimic TTS Service process running!");
		exit(-1); /* can not lock */
	}

	/* first instance continues */

	PID = getpid();
	sprintf(str,"%d\n",getpid());
	write(lfp,str,strlen(str)); /* record pid to lockfile */

	//signal(SIGCHLD,SIG_IGN); /* ignore child */
	signal(SIGTSTP,SIG_IGN); /* ignore tty signals */
	signal(SIGTTOU,SIG_IGN);
	signal(SIGTTIN,SIG_IGN);
	signal(SIGHUP,signalHandler); /* catch hangup signal */
	signal(SIGTERM,signalHandler); /* catch kill signal */

	doLoop();

	exit(0);
}

int main(int argc, char *argv[]) {
	//First, process command line args
	bool makeDaemon = false;
	bool showHelp = false;
	bool showVersion = false;
	customAddressSet = false;
	memset(CUSTOM_ADDRESS, '\0', 200);

    int c;
    opterr = 0;
    ///TODO: Add an option to specify the configuration file to use
	while ((c = getopt (argc, argv, "dhva:")) != -1) {
		switch (c) {
			case 'd':
				makeDaemon = true;
				break;
			case 'h':
				showHelp = true;
				break;
			case 'v':
				showVersion = true;
				break;
			case 'a':
				customAddressSet = true;
				unsigned short l;
				l = (strlen(optarg) > 199) ? 199 : strlen(optarg);
				strncpy(CUSTOM_ADDRESS, optarg, l);
				break;
			case '?':
				if (isprint (optopt)) {
					fprintf (stderr, "Unknown option `-%c'.\n", optopt);
				}
				else {
					fprintf (stderr, "Unknown option character `\\x%x'.\n", optopt);
				}
				return 1;
			default:
				exit(-1);
		}
	}

	if(showHelp) {
		std::cout << "Pyramid ASR Service" << std::endl << std::endl;
		std::cout << "\tThe Pyramid ASR Service provides a DBus interface for using the CMU pocket sphinx speech to text engine." << std::endl;
		std::cout << "This program should be run as root because it is a daemon." << std::endl;
		std::cout << std::endl << "Usage Instructions:" << std::endl;
        std::cout << "\tpyramid [ -h | -v | -d | -a ADDRESS ]" << std::endl << std::endl;
        std::cout << "\t-h\t\tShow this usage text." << std::endl;
        std::cout << "\t-d\t\tStart a new Pyramid ASR Service daemon if one is not running already."<< std::endl;
        std::cout << "\t-v\t\tDisplay the current version of this program." << std::endl;
        std::cout << "\t-a ADDRESS\tConnect the Pyramid ASR Service to the specified custom DBus bus specified by the given address." << std::endl << std::endl;
        std::cout << "If no options are supplied, a new Pyramid ASR Service instance will be made unless another Pyramid ASR Service instance is already running." << std::endl;
        std::cout << "Daemonization will fail if another Pyramid ASR Service instance is running." << std::endl;
        std::cout << "Addresses specified by the -a option should be DBus specification compliant, for example: pyramid -a unix:/tmp/pyramid.socket" << std::endl << "\tSpecifies to connect to a DBus through a UNIX socket located at /tmp/pyramid.socket" << std::endl;
		return 0;
	}

	if(showVersion) {
		std::cout << "Version " << PYRAMID_VERSION << std::endl;
		return 0;
	}

	///TODO: Test this.
	//Check to see if the PYRAMID_RUNNING_DIRECTORY variable is set. If it is, use its value for the daemon's running directory
	gchar ** environmentVarList = g_get_environ();
	if(environmentVarList != NULL) {
		const gchar * runningDirectoryVariable = g_environ_getenv(environmentVarList, "PYRAMID_RUNNING_DIRECTORY");
		if(runningDirectoryVariable != NULL) {
			memset(RUNNING_DIR, '\0', sizeof(char) * 200);
			strncpy(RUNNING_DIR, runningDirectoryVariable, 200);
		}
	}

    //Check that the running directory is present
    struct stat sb;
    if (stat(RUNNING_DIR, &sb) == 0 && S_ISDIR(sb.st_mode)) {
        //Do nothing b/c it exists
    } else {
        std::cerr << "Running directory " << RUNNING_DIR << " not found! Exiting!" << std::endl;
        return -1;
    }

	int res = chdir(RUNNING_DIR); /* change running directory */
	if(res < 0) {
		std::cerr << "Failed to change to running directory: " << RUNNING_DIR << std::endl;
		switch(errno){
			case EACCES:
				std::cerr << "Permission denied to change to the running directory." << std::endl;
				break;
			case ELOOP:
				std::cerr << "A symbolic link loop exists when resolving the target directory" << std::endl;
				break;
			case ENOTDIR:
				std::cerr << "The target directory is not a directory." << std::endl;
				break;
			case ENOENT:
				std::cerr << "No such directory" << std::endl;
				break;
			case ENAMETOOLONG:
				std::cerr << "Target directory path is too long" << std::endl;
				break;
			default:
				std::cerr << "Unknown error case!" << std::endl;
				break;
		}
		return res;
	}

	registerSignalHandles();

	int lfp = open(LOCK_FILE, O_RDWR | O_CREAT, 0640);

	//Attempt to open up the lock file
	if (lfp<0) {
		std::cerr << "Error opening lock file!" << std::endl;

		//syslog(LOG_ERR, "Error opening lock file!");
		exit(1); /* can not open */
	}

	if(lockf(lfp,F_TEST,0)<0) { //Test to see if this is the only instance running

        //There is another process running already
        if(makeDaemon) {
            std::cerr << "There is another process running already that has locked the lockfile! Unable to start the daemon! Exiting..." << std::endl;
            exit(-1);
        }
        else {
            openlog("pyramid", LOG_NDELAY | LOG_PID | LOG_CONS, LOG_USER);
            #ifdef ENABLE_DEBUG
                std::cout << "Debug logging has been enabled" << std::endl;
                setlogmask(LOG_UPTO(LOG_DEBUG));
            #else
                setlogmask(LOG_UPTO(LOG_WARNING));
            #endif
            doLoop();
        }
	}
	else {

        // This is the only process running, make a new instance or make a new daemon
		if(makeDaemon) {
		    openlog("pyramid", LOG_NDELAY | LOG_PID | LOG_CONS, LOG_USER);
            #ifdef ENABLE_DEBUG
                std::cout << "Debug logging has been enabled" << std::endl;
                setlogmask(LOG_UPTO(LOG_DEBUG));
            #else
                setlogmask(LOG_UPTO(LOG_WARNING));
            #endif
			daemonize();
		}
		else {
            //Lock the lock file and then continue
            if (lockf(lfp,F_TLOCK,0)<0) {
                std::cerr << "Failed to lock the lock file! Make sure this program isn't already running!" << std::endl;
                exit(-1);
            }

            openlog("pyramid", LOG_NDELAY | LOG_PID | LOG_CONS, LOG_USER);
            #ifdef ENABLE_DEBUG
                std::cout << "Debug logging has been enabled" << std::endl;
                setlogmask(LOG_UPTO(LOG_DEBUG));
            #else
                setlogmask(LOG_UPTO(LOG_WARNING));
            #endif
            doLoop();
		}
	}

	return 0;
}
