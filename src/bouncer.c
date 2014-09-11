/* **************************************************** *\
 * bouncer.c                                            *
 *                                                      *
 * Project: bouncer                                     *
 * Author:  Christian Weber (ChristianWeber802@gmx.net) *
\* **************************************************** */

#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <sys/select.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>

#ifndef VERSION
	#error "Please define VERSION string!"
#endif

static sig_atomic_t event_loop = 1;
static int          verbose    = 0;
static int          no_bounce  = 0;
static int          all        = 0;
static unsigned     timeout    = 60;

static char         **pattern;
static int          npatterns;
static pid_t        *pid_list;
static int          npid;

#define PID_POLLING_INTERVAL   500 //milliseconds

#define _malloc( pt, type, size)			(pt) = (type*)malloc( size*sizeof(type))
#define _realloc( pt, type, size)           (pt) = (type*)realloc( (pt), size*sizeof(type))

#define _verbose(...)              {if( verbose ) fprintf( stderr, __VA_ARGS__);}


/*
 * What could it be, eh.
 */
static void
print_usage()
{
	fputs( \
	"usage: bouncer [OPTIONS]\n"\
	"    -h, --help          I have no idea\n"\
	"    -V, --version       Print version info\n"\
	"    -d, --debug         Print some verbose messages\n"\
	"    -n, --no-bounce     Don't send any events, just detect the windows.\n"\
	"    -a, --all           Affect all windows.\n"\
	"    -t, --timeout       Time to wait before program exits (default = 30).\n"\
	"    -p, --pattern       Specify patterns on command line.\n", stderr);
};


/*
 * Copys src into dest, returns a pointer to the terminating null character.
 */
static char
*cpycat( char *dest, char *src)
{
	while( *src != '\0' )
		*dest++ = *src++;
	
	*dest = '\0';
	
	return dest;
};


/*
 * Adds a string to the array of patterns.
 * 
 * Parameters: string - String to be added.
 */
static void
add_pattern( char *string)
{
	npatterns++;
	
	_realloc( pattern, char*, npatterns);
	_malloc( pattern[npatterns-1], char, strlen( string) + 1);
	
	cpycat( pattern[npatterns-1], string);
};


/*
 * Frees the patterns array.
 */
static void
free_pattern()
{
	int i;
	
	
	for( i = 0; i < npatterns; i++ )
		free( pattern[i]);
	
	free( pattern);
};


/*
 * Command line parsing.
 */
static void
parse_opt( int argc, char *argv[])
{
	long hlp_timeout;
	char *end_ptr;
	
	
	/******** argument parsing ********/
	while( 1 )
	{
		const char          optstring[] = "hVdnat:p:";
		const struct option long_opts[] =
		{
			{ "help"     , no_argument      , NULL, 'h' },
			{ "version"  , no_argument      , NULL, 'V' },
			{ "debug"    , no_argument      , NULL, 'd' },
			{ "no-bounce", no_argument      , NULL, 'n' },
			{ "all"      , no_argument      , NULL, 'a' },
			{ "timeout"  , required_argument, NULL, 't' },
			{ "pattern"  , required_argument, NULL, 'p' },
			{ NULL       , 0                , NULL, 0   }
		};
		char opt = getopt_long( argc, argv, optstring, long_opts, NULL);
		
		if( opt == -1 )
			break;
			
		switch( opt )
		{
			default:
				print_usage();
				exit(1);
				
			case 'h':
				print_usage();
				exit(0);
			
			case 'V':
				fputs( "bouncer "VERSION"\n", stderr);
				exit(0);
			
			case 'd':
				verbose = 1;
				break;
			
			case 'n':
				no_bounce = 1;
				break;
			
			case 'a':
				all = 1;
				add_pattern( "");
				break;
			
			case 't':
				hlp_timeout = strtol( optarg, &end_ptr, 0);
				
				if( *end_ptr != '\0' ) {
					fprintf( stderr, "'%s' is not a valid number.\n", optarg);
					exit(1);
				}
				if( hlp_timeout < 0 ) {
					fputs( "No negative values for timeout allowed.\n", stderr);
					exit(1);
				}
				if( hlp_timeout > UINT_MAX ) {
					fprintf( stderr, "Maximum value for timeout is %d.\n", UINT_MAX);
					exit(1);
				}
				
				timeout = hlp_timeout;
				break;
			
			case 'p':
				if( !all )
					add_pattern( optarg);
				break;
		}
	}
};


/*
 * Reads the ~/.bouncerc file.
 * 
 * Returns: 0 on succes, -1 on error.
 */
static int
read_config()
{
	char    *home = getenv( "HOME");
	char    buffer[FILENAME_MAX];
	int     line = 1;
	FILE    *fconf;
	
	
	if( home == NULL ) {
		fputs( "$HOME Variable not set. Wtf?\n", stderr);
		return -1;
	}
	
	cpycat( cpycat( buffer, home), "/.bouncerc");
	
	if( (fconf = fopen( buffer, "r")) == NULL ) {
		fprintf( stderr, "Opening '%s' - %s\n", buffer, strerror( errno));
		return -1;
	}
	
	_verbose( "Reading patterns\n");
	
	while( fgets( buffer, FILENAME_MAX, fconf) != NULL ) {
		int c;
		
		
		if( strlen( buffer) == (FILENAME_MAX - 1) && buffer[FILENAME_MAX - 2] != '\n' && !feof( fconf) ) {
			fprintf( stderr, "Line %d - Line too long.\n", line);
			while( (c = fgetc( fconf)) != '\n' && c != EOF );
		}
		else {
			// strip \n
			buffer[strlen(buffer)-1] = '\0';
			_verbose( "  %s\n", buffer);
			add_pattern( buffer);
		}
		line++;
	}
	fclose( fconf);
	
	return 0;
};


/*
 * Jumps to the next in a row of \0-seperated strings and returns it.
 */
static char
*shift_string( char *string)
{
	while( *string++ != '\0' );
	return string;
};


/*
 * Find the PID of a process by traversing procfs.
 * 
 * Parameters: name - Name of the process.
 * 
 * Returns: The found PID or 0 when not found.
 */
static pid_t
spoof_pid( char *name)
{
	DIR           *proc = opendir( "/proc");
	struct dirent *d;
	pid_t         ret = 0;
	
	
	while( (d = readdir( proc)) ) {
		pid_t pid;
		FILE  *stat;
		char  buf[256];
		char  *s, *q;
		
		
		/* check if it is a process */
		if( (pid = atoi( d->d_name)) == 0 ) continue;
		
		snprintf( buf, sizeof(buf) - 1, "/proc/%s/stat", d->d_name);
		
		if( (stat = fopen( buf, "r")) == NULL ) continue;
		
		fgets( buf, sizeof(buf) - 1, stat);
		s = buf;
		while( *s++ != ' ' );
		
		if( *s == '(' ) {
			s++;
			q = s;
			while( *s != ')' ) s++;
			*s = '\0';
		}
		else {
			q = s;
			while( *s != ' ' ) s++;
			*s = '\0';
		}
		
		fclose( stat);
		
		if( strcasecmp( q, name) == 0 ) {
			ret = pid;
			break;
		}
	}
	
	closedir( proc);
	
	return ret;
};


/*
 * Get xcb connection and setup.
 * 
 * Parameters: con    - Stores pointer to xcb connection.
 *             screen - Stores pointer to screen structure.
 * 
 * Returns: 0 on success, -1 in case of error.
 */
static int
connext( xcb_connection_t **con, xcb_screen_t **screen)
{
	char                  *displayname = getenv( "DISPLAY");
	int                   scr_nbr;
	xcb_screen_iterator_t scr_iter;
	
	
	if( displayname == NULL ) {
		fprintf( stderr, "Display variable not set. X Server running?\n");
		return -1;
	}
	
	*con = xcb_connect( displayname, &scr_nbr);
	if( xcb_connection_has_error( *con) ) {
		fprintf( stderr, "Could not connect to %s.\n", displayname);
		return -1;
	}
	
	_verbose("Display: \"%s\"\n", displayname);
	
	scr_iter = xcb_setup_roots_iterator( xcb_get_setup( *con));
	while( scr_iter.rem )
	{
		if( scr_nbr == 0 ) {
			*screen = scr_iter.data;
			break;
		}
		
		scr_nbr--;
		xcb_screen_next( &scr_iter);
	}
	
	return 0;
};


/*
 * Signal handler for SIGALRM
 */
static void
timeout_handler( int signo)
{
	if( signo == SIGALRM )
		event_loop = 0;
};


/*
 * Polling pids
 */
static void *
poll_pids( void *args)
{
	struct timespec interval = { PID_POLLING_INTERVAL/1000, PID_POLLING_INTERVAL*1000000};
	int    pid_count = npid;
	
	
	while( event_loop ) {
		int i;
		
		
		for( i = 0; i < npid; i++ ) {
			if( pid_list[i] ) {
				if( kill( pid_list[i], 0) == -1 ) {
					_verbose( "PID %d closed\n", pid_list[i]);
					pid_list[i] = 0;
					if( --pid_count == 0 ) {
						_verbose( "All processes closed.\n");
						return NULL;
					}
				}
			}
		}
		nanosleep( &interval, NULL);
	}
	return NULL;
};

	
int main( int argc, char *argv[])
{
	int      i, ipat;
	int      return_code = 0;
	int	     event_fd;
	fd_set   set;
	uint32_t mask_mask       = XCB_EVENT_MASK_STRUCTURE_NOTIFY;
	int      nwins_todestroy = 0;
	
	pthread_t pid_polling_thread;
	
	/* x connections */
	xcb_connection_t *con;
	xcb_screen_t     *screen = NULL;
	
	/* client list */
	xcb_intern_atom_cookie_t   client_atom_cookie;
	xcb_intern_atom_reply_t    *client_atom_reply;
	xcb_get_property_cookie_t  client_list_cookie;
	xcb_get_property_reply_t   *client_list_reply;
	xcb_window_t               *client_list;
	int                        nclients;
	
	/* PID list */
	xcb_intern_atom_cookie_t   pid_atom_cookie;
	xcb_intern_atom_reply_t    *pid_atom_reply;
	
	/* closing event */
	#define MASK   XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY|XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT
	xcb_intern_atom_cookie_t   close_atom_cookie;
	xcb_intern_atom_reply_t    *close_atom_reply;
	xcb_client_message_event_t close_event;
	
	/* get window to the front */
	xcb_intern_atom_cookie_t   current_desk_atom_cookie;
	xcb_intern_atom_reply_t    *current_desk_atom_reply;
	xcb_get_property_cookie_t  current_desk_cookie;
	xcb_get_property_reply_t   *current_desk_reply;
	xcb_intern_atom_cookie_t   win_desk_atom_cookie;
	xcb_intern_atom_reply_t    *win_desk_atom_reply;
	xcb_client_message_event_t change_desk_event;
	
	/* alarm handling */
	struct sigaction taction;
	
	
	parse_opt( argc, argv);
	
	/** verbose messages **/
	if( verbose ) {
		fprintf( stderr, "Verbose mode\nTimeout: %d seconds\n", timeout);
		if( no_bounce )
			fputs( "No actual action will be performed.\n", stderr);
		
		if( !all ) {
			fputs( "Patterns from command line:\n", stderr);
			for( ipat = 0; ipat < npatterns; ipat++ )
				fprintf( stderr, "  %s\n", pattern[ipat]);
		}
	}
	
	if( !all ) {
		if( read_config() == -1 && npatterns == 0 )
			goto err_pat;
	}
	
	/** connect to X **/
	if( connext( &con, &screen) == -1 )
		goto err_pat;
	
	
	/** get xcb infos **/
	// requests for atoms
	client_atom_cookie       = xcb_intern_atom( con, 1, 16, "_NET_CLIENT_LIST");
	pid_atom_cookie          = xcb_intern_atom( con, 1, 11,"_NET_WM_PID");
	close_atom_cookie        = xcb_intern_atom( con, 1, 17, "_NET_CLOSE_WINDOW");
	current_desk_atom_cookie = xcb_intern_atom( con, 1, 20, "_NET_CURRENT_DESKTOP");
	win_desk_atom_cookie     = xcb_intern_atom( con, 1, 15, "_NET_WM_DESKTOP");
	
	// get atoms
	client_atom_reply       = xcb_intern_atom_reply( con, client_atom_cookie, NULL);
	pid_atom_reply          = xcb_intern_atom_reply( con, pid_atom_cookie, NULL);
	close_atom_reply        = xcb_intern_atom_reply( con, close_atom_cookie, NULL);
	current_desk_atom_reply = xcb_intern_atom_reply( con, current_desk_atom_cookie, NULL);
	win_desk_atom_reply     = xcb_intern_atom_reply( con, win_desk_atom_cookie, NULL);
	
	// send request for client_list
	current_desk_cookie = xcb_get_property( con, 0, screen->root, current_desk_atom_reply->atom, XCB_ATOM_CARDINAL, 0, 1000L);
	client_list_cookie  = xcb_get_property( con, 0, screen->root, client_atom_reply->atom, XCB_ATOM_WINDOW, 0, 1000L);
	
	// prepare close_event
	close_event.response_type  = XCB_CLIENT_MESSAGE;
	close_event.format         = 32;
	close_event.type           = close_atom_reply->atom;
	close_event.sequence       = 0;
	memset( close_event.data.data32, 0, 5);
	
	// prepare change_desk_event
	current_desk_reply = xcb_get_property_reply( con, current_desk_cookie, NULL);
	change_desk_event.response_type  = XCB_CLIENT_MESSAGE;
	change_desk_event.format         = 32;
	change_desk_event.type           = win_desk_atom_reply->atom;
	change_desk_event.sequence       = 0;
	memset( change_desk_event.data.data32, 0, 5);
	change_desk_event.data.data32[0] = *(uint32_t*)xcb_get_property_value( current_desk_reply);
	
	// get client list
	client_list_reply = xcb_get_property_reply( con, client_list_cookie, NULL);
	nclients = xcb_get_property_value_length( client_list_reply) / sizeof(xcb_window_t);
	client_list = (xcb_window_t*)xcb_get_property_value( client_list_reply);
	
	
	/** iterate through all windows **/
	for( i = 0; i < nclients; i++ ) {
		int j;
		xcb_get_property_cookie_t wm_class_cookie = xcb_get_property( con, 0, client_list[i], XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 0, 1000L);
		xcb_get_property_reply_t  *wm_class_reply = xcb_get_property_reply( con, wm_class_cookie, NULL);
		char                      *wm_class       = xcb_get_property_value( wm_class_reply);
		
		
		if( all ) {
			_realloc( pattern[0], char, strlen( wm_class));
			cpycat( pattern[0], wm_class);
		}
		/** itarate through all patterns **/
		for( j = 0; j < npatterns; j++ ) {
			if( strcmp( pattern[j], wm_class) == 0 || strcmp( pattern[j], shift_string( wm_class)) == 0 ) {
				xcb_get_property_cookie_t pid_cookie = xcb_get_property( con, 0, client_list[i], pid_atom_reply->atom, XCB_ATOM_CARDINAL, 0, 1000L);
				xcb_get_property_reply_t  *pid_reply = xcb_get_property_reply( con, pid_cookie, NULL);
				pid_t                     pid;
				
				
				_verbose( "0x%08x - \"%s\" \"%s\"\n", client_list[i], wm_class, shift_string(wm_class));
					
				nwins_todestroy++;
				
				// obtain pid
				if( pid_reply ) {
					pid = *(pid_t*)xcb_get_property_value( pid_reply);
					
					_verbose( "  PID: %d\n", pid);
				}
				// fallback
				else {
					_verbose( "  _NET_WM_PID not set...\n");
					
					if( (pid = spoof_pid( pattern[j])) != 0 )
						_verbose( "  PID via /proc: %d\n", pid)
					else
						_verbose( "  Could not retrieve PID...\n");
				}
				
				if( pid ) {
					_realloc( pid_list, pid_t, npid + 1);
					pid_list[npid++] = pid;
				}
				
				// send change_desk_event and close_event
				xcb_change_window_attributes( con, client_list[i], XCB_CW_EVENT_MASK, &mask_mask);
				if( no_bounce == 0 ) {
					change_desk_event.window = client_list[i];
					close_event.window       = client_list[i];
					xcb_send_event( con, 0, screen->root, MASK, (char*)&close_event);
					xcb_send_event( con, 0, screen->root, MASK, (char*)&change_desk_event);
				}
			}
		}
	}
	
	if( nwins_todestroy == 0 ) {
		_verbose( "No windows found. Exiting...\n");
		goto err_all;
	}
	
	taction.sa_handler = timeout_handler;
	sigemptyset( &taction.sa_mask);
	taction.sa_flags = 0;
	sigaction( SIGALRM, &taction, NULL);
	sigaction( SIGTERM, &taction, NULL);
	sigaction( SIGINT , &taction, NULL);
	alarm( timeout);
	
	/** event loop **/
	pthread_create( &pid_polling_thread, NULL, &poll_pids, NULL);
	
	event_fd = xcb_get_file_descriptor( con);
	FD_ZERO( &set);
	FD_SET( event_fd, &set);
	xcb_flush( con);
	while( event_loop )
	{
		xcb_generic_event_t *event = xcb_poll_for_event( con);
		
		if( event == NULL ) {
			if( select( event_fd + 1, &set, NULL, NULL, NULL) == -1 ) {
				if( errno == EINTR && event_loop == 0) {
					_verbose( "Timeout reached. Exiting...\n");
				}
				else {
					perror( "select()");
					return_code = 1;
					goto err_all;
				}
				break;
			}
		}
		else {
			if( event->response_type == XCB_DESTROY_NOTIFY ) {
				_verbose( "0x%08x closed...\n", ((xcb_destroy_notify_event_t*)event)->window);
					
				if( --nwins_todestroy == 0 ) {
					_verbose( "All windows closed.\n");
					break;
				}
			}
			free( event);
		}
	}
	
	pthread_join( pid_polling_thread, NULL);
	_verbose( "Exiting...");
	
  err_all:
	free( client_list_reply);
	free( pid_atom_reply);
	free( current_desk_reply);
	free( win_desk_atom_reply);
	free( current_desk_atom_reply);
	free( close_atom_reply);
	free( client_atom_reply);
	free( pid_list);
	
	xcb_disconnect( con);

  err_pat:
	free_pattern();
	
	
	return return_code;
};
	
	
		
