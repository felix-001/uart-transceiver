/**********************************
 * author:felix
 *  date:2016-07-23
 * 
 * ***********************************/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>   
#include <sys/stat.h>     
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <signal.h>
/*
----------------------------------------------------------------------------
| file name length (4bytes) | file name | file length (4bytes) | file data |
----------------------------------------------------------------------------
*/

#define DEFAULT_PORT "/dev/ttyS2"
#define LOG(args...) printf("%s() %d ", __FUNCTION__, __LINE__);printf(args);
#define ERR(args...)  printf("%s() %d ", __FUNCTION__, __LINE__);printf(args);
#define LEN_SZ 4
#define FILE_NAME_LEN 128
#define READ_SZ 1024

char *g_read_data = NULL;
size_t g_file_len = 0;

typedef struct {
	char *name;
	char *dev;
} uart_list_t;
typedef unsigned short uint16;
static uart_list_t g_uart_list[] = 
{
	{ "uart0", "/dev/ttySAC0"   },
	{ "uart1", "/dev/ttySAC1"   },
	{ "uart2", "/dev/ttySAC2"   },
	{ "uart3", "/dev/ttySAC3"   },
    { "usb0",  "/dev/ttyUSB0" },
    { "usb1",  "/dev/ttyUSB1" },
};

void usage()
{
	printf("usage:\n");
	printf("uart server/client [serial port] [file name]\n");
	printf("[serial port]\n");
	printf("uart0 uart1 uart2 uart3\n");
	printf("[file name]\n");
	printf("only uart client need this\n");
}

void sig_hldr( int sig_no )
{
    int i = 0;

    switch( sig_no ) {
    case 3:
        printf("SIGQUIT\n");
        break;
    case 2:
        printf("SIGINT, ctrl+C\n");
        printf("the data :\n");
        for ( i=0; i<g_file_len; i++ ) {
            printf("0x%02x, ", g_read_data[i]);
            if ( (i+1)%16 == 0)
                printf("\n");
        }
        printf("\n");
        free(g_read_data);
        exit(1);
        break;
    }
}

int uart_open( char *port )
{
	int fd = 0;

	fd = open( port, O_RDWR|O_NOCTTY|O_NDELAY );
	if ( -1 == fd ) {
		ERR("open port %s error, %s\n", port, strerror(errno));
		return -1;
	}

	/*if( fcntl( fd, F_SETFL, 0 ) < 0 ) {*/
		/*ERR("fcntl error, errno = %d\n", errno);*/
		/*return -1;*/
	/*}*/

	if( 0 == isatty( STDIN_FILENO ) ) {
		ERR("standard input is not a terminal device\n");
		return -1;
	}

	LOG("open port %s success\n", port);
	
	return fd;
}

int uart_config( int fd, int baudrate, int flow_ctrl, int databits, int stopbits, int parity )  
{     
	int   i;  
    int   status;  
    struct termios options;  
     
    if  ( tcgetattr( fd, &options ) != 0 ) {  
          ERR("tcgetattr error, %s\n", strerror(errno));      
          return (-1);   
    }
    
    cfsetispeed( &options, baudrate );   
    cfsetospeed( &options, baudrate );    
     
    options.c_cflag |= CLOCAL;  
    options.c_cflag |= CREAD;  
    
    switch(flow_ctrl)  
    {       
       case 0 :
			options.c_cflag &= ~CRTSCTS;  
			break;     
        
       case 1 :
			options.c_cflag |= CRTSCTS;  
			break;  
       case 2 :
			options.c_cflag |= IXON | IXOFF | IXANY;  
			break;  
    }
	
    options.c_cflag &= ~CSIZE;  
    switch (databits)  
    {    
       case 5 :  
			options.c_cflag |= CS5;  
			break;  
       case 6 :  
			options.c_cflag |= CS6;  
			break;  
       case 7 :      
			options.c_cflag |= CS7;  
			break;  
       case 8 :      
			options.c_cflag |= CS8;  
			break;    
       default:     
			ERR("Unsupported data size\n");  
			return (-1);   
    }
	
    switch (parity)  
    {    
       case 'n':  
       case 'N':
			options.c_cflag &= ~PARENB;   
			options.c_iflag &= ~INPCK;      
			break;   
       case 'o':    
       case 'O':
			options.c_cflag |= (PARODD | PARENB);   
			options.c_iflag |= INPCK;               
			break;   
       case 'e':   
       case 'E':
			options.c_cflag |= PARENB;         
			options.c_cflag &= ~PARODD;         
			options.c_iflag |= INPCK;        
			break;  
       case 's':  
       case 'S':
			options.c_cflag &= ~PARENB;  
			options.c_cflag &= ~CSTOPB;  
			break;   
        default:    
			ERR("Unsupported parity\n");      
			return (-1);   
    }   
    options.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    
    switch (stopbits)  
    {    
       case 1:     
			options.c_cflag &= ~CSTOPB;
			break;   
       case 2:     
			options.c_cflag |= CSTOPB; 
			break;  
       default:     
			ERR("Unsupported stop bits\n");   
			return (-1);  
    }  
     
	options.c_oflag &= ~OPOST;
	options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);     
    options.c_cc[VTIME] = 1;
    options.c_cc[VMIN] = 1;
     
    tcflush( fd, TCIOFLUSH );  
     
    if ( tcsetattr( fd, TCSANOW, &options ) != 0 ) {
		ERR("com set error!\n");    
		return (-1);   
    }  
    return (0);   
}  

ssize_t uart_read( int fd, char *out, size_t inlen )
{
	size_t left = 0;
	ssize_t nread = 0;
    size_t total = 0;
	
	if ( !out ) {
		ERR("check param error\n");
		return -1;
	}

	left = inlen;
	while ( left > 0 ) {
		if ( ( nread = read( fd, out, left ) ) < 0 ) {
            if ( errno == EAGAIN ) {
                continue;
            }
			if ( left == inlen ) {
				ERR("read error, errno = %d, read = %d\n", errno, nread);
				return (-1);
			} else {
                LOG("break, errno = %d\n", errno);
				break;
			}
		} else if ( nread == 0 ) {
            LOG("EOF\n");
			break;/* EOF */
		} else {
            //LOG("nread = %d\n", nread);
            total += nread;
//            LOG("total = %d\n", total);
        }
		
		left -= nread;
		out += nread;
	}

	return (inlen - left);	
}

ssize_t uart_write( int fd, char *in, size_t inlen )
{
	size_t left = 0;
	ssize_t written = 0;

	if ( !in ) {
		ERR("check param error\n");
		return -1;
	}

	left = inlen;
	while ( left > 0 ) {
        /*tcflush(fd, TCIOFLUSH);*/
		if ( ( written = write( fd, in, left )) < 0 ) {
            if ( errno == EAGAIN) {
                continue;
            }
			if ( left == inlen ) {
				ERR("write error, errno = %d, written = %d\n", errno, written);
				return (-1);
			} else {
				break;
			}
		} else if ( written == 0 ) {
			break;/* EOF */
		} else {
            /*LOG("written = %d\n", written);*/
        }
		
		left -= written;
		in += written;
	}

	return (inlen - left);
}

void uart_server_start( char *port )
{
	int fd = 0 ;
	char file_name[FILE_NAME_LEN] = { 0 };
	char file_data[READ_SZ] = { 0 };
	FILE *fp = NULL;
	size_t data_sz = 0, file_name_len = 0, left = 0, written = 0;
	ssize_t read = 0, ret = 0;

	fd = uart_open( port );
	if ( fd < 0 ) {
		return;
	}

    ret = uart_config( fd, B115200, 0, 8, 1, 'n' );
	if ( 0 != ret ) {
		return;
	}

	ret = uart_read( fd, (char *)&file_name_len, LEN_SZ );
    LOG("ret = %d\n", ret);
	if ( ret != LEN_SZ ) {
		ERR("uart_read error, ret = %d\n", ret);
		return;
	}
	LOG("file_name_len = %d\n", file_name_len);
	
	ret = uart_read( fd, file_name, file_name_len );
	if ( ret != file_name_len ) {
		ERR("uart_read error, ret = %d\n", ret);
		return;
	}	
	LOG("file_name = %s\n", file_name);
	fp = fopen( file_name, "wb+" );
	if ( NULL == fp ) {
		ERR("fopen error, errno = %d\n", errno);
		return;
	}
	
	ret = uart_read( fd, (char *)&data_sz, LEN_SZ );
	if ( ret != LEN_SZ ) {
		ERR("uart_read error, ret = %d\n", ret);
		return;
	}
	LOG("data_sz = %d\n", data_sz);
    g_file_len = data_sz;
    g_read_data = malloc( data_sz );
    if ( NULL == g_read_data ) {
        ERR("get memory %d size error\n", data_sz );
        goto err;
    }
    read = uart_read( fd, g_read_data, data_sz );
    if ( read != data_sz ) {
        ERR("uart_read error, read = %d\n", read);
        goto err;
    }
    written = fwrite( g_read_data, data_sz, 1, fp);
    if ( written != 1 ) {
        ERR(" fwrite error, written = %d\n", written);
        goto err;
    }

err:
    free( g_read_data );
    close( fd );
	fclose( fp );
	LOG("finish to download file %s\n", file_name);
}

void uart_client_start( char *port, char *file_name)
{
	int fd = 0, ret = 0;
	FILE *fp = NULL;
	struct stat sts;
	unsigned long filesz = 0;
	size_t left = 0, read = 0;
	ssize_t written = 0;
	unsigned char buf[READ_SZ] = { 0 };
	int filename_len = 0;
    int i = 0;
		
	fd = uart_open( port );
	if ( !fd ) {
		return;
	}

    ret = uart_config( fd, B115200, 0, 8, 1, 'n' );
	if ( 0 != ret ) {
		return;
	}

	ret = stat( file_name, &sts );
	if ( ret < 0 ) {
		ERR("stat error, ret = %d\n", ret);
		return;
	}

	filesz = sts.st_size;
	LOG("the size of file %s is %ld\n", file_name, filesz);
	
	fp = fopen( file_name, "rb" );
	if ( NULL == fp ) {
		ERR("open file %s error\n", file_name);
		return;
	}

	filename_len = strlen( file_name );
	written = uart_write( fd, (char *)&filename_len, 4 );
	if ( written != 4 ) {
		ERR("uart_write() error, written = %d\n", written);
		return;
	}

	written = uart_write( fd, file_name, strlen(file_name) );
	if ( written != strlen(file_name) ) {
		ERR("uart_write() error, written = %d\n", written);
		return;
	}

	written = uart_write( fd, (char *)&filesz, 4 );
	if ( written != 4 ) {
		ERR("uart_write() error, written = %d\n", written);
		return;
	}

	left = filesz;
	while ( left > 0 ) {
        read = fread( buf, 1, 100, fp );
		if ( 100 != read) {
			if ( read > 0 ) {
				written = uart_write( fd, buf, read );
				if ( written != read ) {
					ERR("uart_write() error, written = %d\n", written);
					return;
				}
                usleep(10000);
			} else {
				ERR("fread error, read = %d\n", read);
				return;
			}
		} else {
            /*for ( i=0; i<10; i++ ) {*/
                /*printf("0x%02x, ", buf[i]);*/
            /*}*/
			written = uart_write( fd, buf, read );
			if ( written != read ) {
				ERR("uart_write() error, written = %d\n", written);
				return;
			}
            usleep(10000);
		}

		left -= read;
        LOG("left = %d\n", left);
	}

    fclose(fp);
    close(fd);

	LOG("send file %s to server OK!\n", file_name);
}

int main( int argc, char **argv )
{
	uint16 sz = sizeof(g_uart_list)/sizeof(g_uart_list[0]);
	uart_list_t *p_list = g_uart_list;
	uint16 i = 0;
	char *p_port = NULL;
	char file_name[FILE_NAME_LEN] = { 0 };

    signal( SIGINT, sig_hldr );
    signal( SIGQUIT, sig_hldr );
	
	if ( argc > 2 ) {
		for ( i=0; i<sz; i++ ) {
            p_list = &g_uart_list[i];
			if ( strcmp( p_list->name, argv[2] ) == 0 ) {
				p_port = p_list->dev;
				break;
			}
		}
		if ( !p_port ) {
			ERR("check param error, param = %s\r\n", argv[1]);
			usage();
			return 0;
		}
	} else {
		p_port = DEFAULT_PORT;
	}

    if ( argv[1] ) {
        if ( strcmp( argv[1], "server" ) == 0 ) {
            uart_server_start( p_port );
        } else if ( strcmp( argv[1], "client") == 0 ) {
            if ( !argv[3] ) {
                ERR("have't pass file name param\n");
                return 0;
            }
            uart_client_start( p_port, argv[3] );
        } else {
            ERR("uart mode param error\n");
            return 0;
        }
    } else {
        usage();
    }
    

	return 0;
}
