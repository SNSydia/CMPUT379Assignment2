/*
** selectserver.c -- a cheezy multiperson chat server
** Link: http://beej.us/guide/bgnet/output/html/multipage/advanced.html#select
** Modified by: mwaqar
** 		Blaz Pocrnja
*/

#include "chat.h"

int main(void)
{
    fd_set master;    // master file descriptor list
    fd_set read_fds;  // temp file descriptor list for select()
    int fdmax;        // maximum file descriptor number

    int listener;     // listening socket descriptor
    int newfd;        // newly accept()ed socket descriptor
    struct sockaddr_in sa;
    struct sockaddr_in remoteaddr; // client address
    socklen_t addrlen;

    char buf[256];    // buffer for client data
    int nbytes;

    int yes=1;        // for setsockopt() SO_REUSEADDR, below
    int i, j, rv, k, l;

    FD_ZERO(&master);    // clear the master and temp sets
    FD_ZERO(&read_fds);

    unsigned short clients = 0;
    unsigned short outnum;
    int pid;
    char outbyte;
    char length;
    unsigned short msglength;
    char tmpname[MAX_NAME];
    typedef enum { false, true } bool;

    struct timeval tv;      

    time_t activitytime[FD_SETSIZE] = {0}; 
    time_t current = 0;

    /* Declare shared memory variables */
    key_t key;
    int shmid;
    typedef char name[FD_SETSIZE][MAX_NAME];
    name *usernames;				//Array of usernames for each possible file descriptor

    /* Initialize Shared Memory */
    key = ftok("server.c",'R');
    shmid = shmget(key, FD_SETSIZE*MAX_NAME, 0644 | IPC_CREAT);

    /* Attach to Shared Memory */
    usernames = shmat(shmid, (void *)0, 0);

    if(usernames == (name *)(-1))
    {
        perror("shmat");
    }

    listener = socket(AF_INET, SOCK_STREAM, 0);

    // get us a socket and bind it
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port = htons(MY_PORT);

    if (bind(listener, (struct sockaddr *)&sa, sizeof(sa)) < 0)
    {
        perror("bind");
        exit(-1);
    }

    // listen
    if (listen(listener, 10) == -1)
    {
        perror("listen");
        exit(-1);
    }

    // add the listener to the master set
    FD_SET(listener, &master);

    // keep track of the biggest file descriptor
    fdmax = listener; // so far, it's this one

    // main loop
    for(;;)
    {
        read_fds = master; // copy it

        //Select makes tv undefined. So reset it
        tv.tv_sec = 0;
        tv.tv_usec = 0;

        if (select(fdmax+1, &read_fds, NULL, NULL, &tv) == -1)
        {
            perror("select");
            exit(-1);
        }

        //Update Current Time
        current = time(NULL);

        // run through the existing connections looking for data to read
        for(i = 0; i <= fdmax; i++)
        {

            if(i > listener)
            {
                if((*usernames)[i-listener-1][0] != 0 && (difftime(current,activitytime[i-listener-1]) >= TIMEOUT))
                {
                    close(i);                   // bye!
                    FD_CLR(i, &master);         //remove from master set
                    FD_CLR(i, &read_fds);       //remove from read set
                    --clients;                  //Decrement number of clients connected
                    (*usernames)[i-listener-1][0] = 0; //Nullify username
                    //TODO Send Disconnect Message
                    printf("%d Timed Out\n", i);
                    continue;
                }
            }

            if (FD_ISSET(i, &read_fds))   // we got one!!
            {
                if (i == listener)
                {
                    // handle new connections
                    addrlen = sizeof remoteaddr;
                    newfd = accept(listener, (struct sockaddr *)&remoteaddr, &addrlen);

                    if (newfd == -1)
                    {
                        perror("accept");
                    }
                    else
                    {

                        FD_SET(newfd, &master); // add to master set

                        if (newfd > fdmax)      // keep track of the max
                        {
                            fdmax = newfd;
                        }

                        printf("selectserver: new connection from %s:%d on socket %d\n",
                               inet_ntoa(remoteaddr.sin_addr), ntohs(remoteaddr.sin_port), newfd);

                        //---------HANDSHAKE PROTOCOL------------

                        //First Two Bytes
                        buf[0] = HAND_1;
                        buf[1] = HAND_2;

                        if(my_send(newfd, buf, 2, 0) != 2)
                        {
                            perror("Handshake Bytes not sent!");
                        }

                        //Number of Clients
                        outnum = htons(clients);		//Change byte order before sending to client

                        if(my_send(newfd, &outnum, sizeof(outnum), 0) != 2)
                        {
                            perror("Number of clients not sent!");
                        }

                        //Usernames
                        int clienttmp = clients;

                        for(k = 0; k < clienttmp; ++k)
                        {
                            //if user is still connected
                            if((*usernames)[k][0] != 0)
                            {
                                //Send length
                                outbyte = (*usernames)[k][0];

                                if(my_send(newfd, &outbyte, sizeof(outbyte), 0) != sizeof(outbyte))
                                {
                                    perror("Length of username not sent!");
                                }

                                //Send array of chars
                                for(l = 1; l <= (int)outbyte; ++l)
                                {
                                    buf[l - 1] = (*usernames)[k][l];
                                }

                                if(my_send(newfd, buf, (int)outbyte, 0) != (int) outbyte)
                                {
                                    perror("Username not sent!");
                                }
                            }
                            else
                            {
                                clienttmp++;
                            }

                        }

                        (*usernames)[newfd-listener-1][0] = 0; //Nullify username

                    }
                }
                else
                {
                    //handle data from a client

                    //Check if connection closed
                    if ((nbytes = my_recv(i, buf, 1, 0)) <= 0)
                    {
                        // got error or connection closed by client
                        if (nbytes == 0)
                        {
                            // connection closed
                            printf("selectserver: socket %d hung up\n", i);
                        }
                        else
                        {
                            perror("Could not receive initial byte!");
                        }

                        close(i); // bye!
                        FD_CLR(i, &master); // remove from master set
                        --clients;		//Decrement number of clients connected


                        //Save length
                        length = (*usernames)[i-listener-1][0];

                        //Nullify UserName
                        (*usernames)[i-listener-1][0] = 0;

                        //Put username in buffer
                        for(k = 1; k <= (int)length; ++k)
                        {
                            buf[k-1] = (*usernames)[i-listener - 1][k];
                        }

                        outbyte = LEAVE_MSG;

                        //Forward disconnection to all clients
                        for(j = listener + 1; j <= fdmax; j++)
                        {
                            // send if name has been set
                            if (FD_ISSET(j, &master) && (*usernames)[j-listener-1][0] != 0)
                            {

                                //Send User Update Message: Leave
                                if(my_send(j, &outbyte, 1, 0) != 1)
                                {
                                    printf("Could not send disconnection message to %d\n", j);
                                }

                                if(my_send(j, &length, 1, 0) != 1)
                                {
                                    printf("Could not send disconnection length to %d\n", j);
                                }

                                if(my_send(j, buf, (int)length, 0) != (int) length)
                                {
                                    printf("Could not send disconnection username to %d\n", j);
                                }
                            }
                        }

                    }
                    else
                    {
                        // we got some data from a client
                        if((*usernames)[i-listener-1][0] == 0) 			//Username Message
                        {

                            //---------Continue Handshake Protocol---------------
                            length = buf[0];

                            //Receive New Username Chars
                            if(my_recv(i, buf, (int)length, 0) != (int) length)
                            {
                                perror("Could not receive new username!");
                            }
			    
                            //Check if Username already exists
            			    bool exists = false;
            			    for(j = 0; j <= fdmax - listener - 1; ++j)
            			    {
            			    	if(length == (*usernames)[j][0])
            				    {
                					for(k = 1; k <= (int) length; ++k)
                					{
                						if(buf[k-1] != (*usernames)[j][k])
                						{
                							exists = false;
                							break;
                						}
                						else
                						{
                							exists = true;
                						}
                								
                					}
            				    }

            				    if(exists == true) break;
            			    }
				
            			    if(!exists)
            			    {
    		                    //Store in usernames array
    		                    (*usernames)[i-listener - 1][0] = length;

    		                    for(k = 1; k <= (int)length; ++k)
    		                    {
    		                        (*usernames)[i-listener - 1][k] = buf[k-1];
    		                        printf("%c",buf[k-1]);
    		                    }
    		                    printf(" stored in array %d\n", i-listener - 1);
    		                    ++clients;				//Increment number of clients connected

    		                    outbyte = JOIN_MSG;

    		                    //Forward new connection to all clients
    		                    for(j = listener + 1; j <= fdmax; j++)
    		                    {
    		                        // send if name has been set
    		                        if (FD_ISSET(j, &master) && (*usernames)[j-listener-1][0] != 0)
    		                        {

    		                            //Send User Update Message: Join
    		                            if(my_send(j, &outbyte, 1, 0) != 1)
    		                            {
    		                                printf("Could not send connection message to %d\n", j);
    		                            }

    		                            if(my_send(j, &length, 1, 0) != 1)
    		                            {
    		                                printf("Could not send connection length to %d\n", j);
    		                            }

    		                            if(my_send(j, buf, (int)length, 0) != (int) length)
    		                            {
    		                                printf("Could not send connection username to %d\n", j);
    		                            }
    		                        }
    		                    }
                            }
                            else
                            {
    				            close(i); // bye!
                            	FD_CLR(i, &master); // remove from master set
    				            perror("Username not unique!\n");
                            }


                        }
                        else 							//Chat Message
                        {
                            //Receive Message Length
                            if(my_recv(i, buf+1, 1, 0) != 1)
                            {
                                perror("Could not recieve the rest of messagelength!");
                            }

                            msglength = (buf[1] << 8) | buf[0];
                            msglength = ntohs(msglength);
                            printf("MessageLength : %d\n", msglength);

                            if(msglength > 0){
                                //Receive Message
                                if(my_recv(i, buf, msglength, 0) != msglength)
                                {
                                    perror("Could not receive chat message!");
                                }

                                printf("Message :");

                                for(k = 0; k < msglength; ++k)
                                {
                                    printf("%c",buf[k]);
                                }

                                printf("\n");

                                //Forward to all clients
                                for(j = listener + 1; j <= fdmax; j++)
                                {
                                    // send if name has been set
                                    if (FD_ISSET(j, &master) && (*usernames)[j-listener-1][0] != 0)
                                    {
                                        //Send Message Code
                                        outbyte = CHAT_MSG;

                                        if (my_send(j, &outbyte, sizeof(outbyte), 0) != sizeof(outbyte))
                                        {
                                            printf("Chat message code not sent to FD %d\n", j);
                                        }

                                        //Send Client info
                                        length = (*usernames)[i-listener-1][0];

                                        for(k = 0; k <= ((int)length); ++k)
                                        {
                                            tmpname[k] = (*usernames)[i-listener-1][k];
                                        }

                                        if(my_send(j, tmpname, ((int)length) + 1, 0) != ((int)length) + 1)
                                        {
                                            printf("Client info not sent to FD %d\n", j);
                                        }

                                        //Send Message Length
                                        unsigned short tmp = htons(msglength);

                                        if (my_send(j, &tmp, sizeof(tmp), 0) != sizeof(tmp))
                                        {
                                            printf("Chat message length not sent to FD %d\n", j);
                                        }

                                        //Send Message
                                        if (my_send(j, buf, msglength, 0) != msglength)
                                        {
                                            printf("Chat message not sent to FD %d\n", j);
                                        }
                                    }
                                }
                            }
                        }
                    }

                    //Record Activity Time
                    if(i > listener)
                    {
                        activitytime[i-listener-1] = time(NULL);
                    }

                } // END handle data from client
            } // END got new incoming connection
        } // END looping through file descriptors
    } // END for(;;)--and you thought it would never end!

    return 0;
}
