/****************************************************************************
 * include/nuttx/net/net.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __INCLUDE_NUTTX_NET_NET_H
#define __INCLUDE_NUTTX_NET_NET_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#ifdef CONFIG_NET

#include <sys/socket.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <semaphore.h>

#include <nuttx/queue.h>
#include <nuttx/mutex.h>
#ifdef CONFIG_MM_IOB
#  include <nuttx/mm/iob.h>
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Capabilities of a socket */

#define SOCKCAP_NONBLOCKING (1 << 0)  /* Bit 0: Socket supports non-blocking
                                       *        operation. */

/* Definitions of 8-bit socket flags */

#define _SF_INITD           0x01  /* Bit 0: Socket structure is initialized */
#define _SF_NONBLOCK        0x08  /* Bit 3: Don't block if no data (TCP/READ only) */
#define _SF_LISTENING       0x10  /* Bit 4: SOCK_STREAM is listening */
#define _SF_BOUND           0x20  /* Bit 5: SOCK_STREAM is bound to an address */
                                  /* Bits 6-7: Connection state */
#define _SF_CONNECTED       0x40  /* Bit 6: SOCK_STREAM/SOCK_DGRAM is connected */
#define _SF_CLOSED          0x80  /* Bit 7: SOCK_STREAM was gracefully disconnected */

/* Connection state encoding:
 *
 *  _SF_CONNECTED==1 && _SF_CLOSED==0 - the socket is connected
 *  _SF_CONNECTED==0 && _SF_CLOSED==1 - the socket was gracefully
 *                                      disconnected
 *  _SF_CONNECTED==0 && _SF_CLOSED==0 - the socket was rudely disconnected
 */

/* Macro to manage the socket state and flags */

#define _SS_INITD(s)        (((s) & _SF_INITD)     != 0)
#define _SS_ISNONBLOCK(s)   (((s) & _SF_NONBLOCK)  != 0)
#define _SS_ISLISTENING(s)  (((s) & _SF_LISTENING) != 0)
#define _SS_ISBOUND(s)      (((s) & _SF_BOUND)     != 0)
#define _SS_ISCONNECTED(s)  (((s) & _SF_CONNECTED) != 0)
#define _SS_ISCLOSED(s)     (((s) & _SF_CLOSED)    != 0)

/* Determine if a socket is valid.  Valid means both (1) allocated and (2)
 * successfully initialized:
 *
 *   Allocated:    psock->s_conn != NULL
 *   Initialized:  _SF_INITD bit set in psock->s_flags
 *
 * This logic is used within the OS to pick the sockets to be cloned when a
 * new task is created.  A complexity in SMP mode is that a socket may be
 * allocated, but not yet initialized when the socket is cloned by another
 * pthread.
 */

#define _PS_ALLOCD(psock)   ((psock)->s_conn != NULL)
#define _PS_INITD(psock)    (_SS_INITD((psock)->s_flags))
#define _PS_VALID(psock)    (_PS_ALLOCD(psock) && _PS_INITD(psock))

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Link layer type.  This type is used with netdev_register in order to
 * identify the type of the network driver.
 */

enum net_lltype_e
{
  NET_LL_ETHERNET = 0, /* Ethernet */
  NET_LL_LOOPBACK,     /* Local loopback */
  NET_LL_SLIP,         /* Serial Line Internet Protocol (SLIP) */
  NET_LL_TUN,          /* TUN Virtual Network Device */
  NET_LL_BLUETOOTH,    /* Bluetooth */
  NET_LL_IEEE80211,    /* IEEE 802.11 */
  NET_LL_IEEE802154,   /* IEEE 802.15.4 MAC */
  NET_LL_PKTRADIO,     /* Non-standard packet radio */
  NET_LL_MBIM,         /* CDC-MBIM USB host driver */
  NET_LL_CAN,          /* CAN/LIN bus */
  NET_LL_CELL          /* Cellular Virtual Network Device */
};

/* This defines a bitmap big enough for one bit for each socket option */

typedef uint32_t sockopt_t;

/* This defines the storage size of a timeout value.  This effects only
 * range of supported timeout values.  With an LSB in seciseconds, the
 * 16-bit maximum of 65535 corresponds to 1 hr 49 min 13.5 sec at decisecond
 * resolution.
 */

typedef uint16_t socktimeo_t;

/* This type defines the type of the socket capabilities set */

typedef uint8_t sockcaps_t;

/* This callbacks are socket operations that may be performed on a socket of
 * a given address family.
 */

struct file;    /* Forward reference */
struct stat;    /* Forward reference */
struct socket;  /* Forward reference */
struct pollfd;  /* Forward reference */

struct sock_intf_s
{
  CODE int        (*si_setup)(FAR struct socket *psock);
  CODE sockcaps_t (*si_sockcaps)(FAR struct socket *psock);
  CODE void       (*si_addref)(FAR struct socket *psock);
  CODE int        (*si_bind)(FAR struct socket *psock,
                    FAR const struct sockaddr *addr, socklen_t addrlen);
  CODE int        (*si_getsockname)(FAR struct socket *psock,
                    FAR struct sockaddr *addr, FAR socklen_t *addrlen);
  CODE int        (*si_getpeername)(FAR struct socket *psock,
                    FAR struct sockaddr *addr, FAR socklen_t *addrlen);
  CODE int        (*si_listen)(FAR struct socket *psock, int backlog);
  CODE int        (*si_connect)(FAR struct socket *psock,
                    FAR const struct sockaddr *addr, socklen_t addrlen);
  CODE int        (*si_accept)(FAR struct socket *psock,
                    FAR struct sockaddr *addr, FAR socklen_t *addrlen,
                    FAR struct socket *newsock, int flags);
  CODE int        (*si_poll)(FAR struct socket *psock,
                    FAR struct pollfd *fds, bool setup);
  CODE ssize_t    (*si_sendmsg)(FAR struct socket *psock,
                    FAR struct msghdr *msg, int flags);
  CODE ssize_t    (*si_recvmsg)(FAR struct socket *psock,
                    FAR struct msghdr *msg, int flags);
  CODE int        (*si_close)(FAR struct socket *psock);
  CODE int        (*si_ioctl)(FAR struct socket *psock,
                    int cmd, unsigned long arg);
  CODE int        (*si_socketpair)(FAR struct socket *psocks[2]);
  CODE int        (*si_shutdown)(FAR struct socket *psock, int how);
#ifdef CONFIG_NET_SOCKOPTS
  CODE int        (*si_getsockopt)(FAR struct socket *psock, int level,
                    int option, FAR void *value, FAR socklen_t *value_len);
  CODE int        (*si_setsockopt)(FAR struct socket *psock, int level,
                    int option, FAR const void *value, socklen_t value_len);
#endif
#ifdef CONFIG_NET_SENDFILE
  CODE ssize_t    (*si_sendfile)(FAR struct socket *psock,
                    FAR struct file *infile, FAR off_t *offset,
                    size_t count);
#endif
};

/* Each socket refers to a connection structure of type FAR void *.  Each
 * socket type will have a different connection structure type bound to its
 * sockets.  The fields at the the beginning of each connection type must
 * begin the same content prologue as struct socket_conn_s and must be cast
 * compatible with struct socket_conn_s.  Connection-specific content may
 * then follow the common prologue fields.
 */

struct devif_callback_s;  /* Forward reference */

struct socket_conn_s
{
  /* Common prologue of all connection structures. */

  dq_entry_t node;        /* Supports a doubly linked list */

  /* This is a list of connection callbacks.  Each callback represents a
   * thread that is stalled, waiting for a device-specific event.
   */

  FAR struct devif_callback_s *list;
  FAR struct devif_callback_s *list_tail;

  /* Socket options */

#ifdef CONFIG_NET_SOCKOPTS
  int16_t       s_error;     /* Last error that occurred on this socket */
  sockopt_t     s_options;   /* Selected socket options */
  socktimeo_t   s_rcvtimeo;  /* Receive timeout value (in deciseconds) */
  socktimeo_t   s_sndtimeo;  /* Send timeout value (in deciseconds) */
#  ifdef CONFIG_NET_SOLINGER
  socktimeo_t   s_linger;    /* Linger timeout value (in deciseconds) */
#  endif
#  ifdef CONFIG_NET_BINDTODEVICE
  uint8_t       s_boundto;   /* Index of the interface we are bound to.
                              * Unbound: 0, Bound: 1-MAX_IFINDEX */
#  endif
#endif

  /* Definitions of 8-bit socket flags */

  uint8_t       s_flags;     /* See _SF_* definitions */

  /* Definitions of IPv4 TOS and IPv6 Traffic Class */

  uint8_t       s_tos;       /* IPv4 Type of Service */
#define s_tclass s_tos       /* IPv6 traffic class definition */
#if defined(CONFIG_NET_IPv4) || defined(CONFIG_NET_IPv6)
  uint8_t       s_ttl;       /* Default time-to-live */
#endif

  /* Connection-specific content may follow */
};

/* This is the internal representation of a socket reference by a file
 * descriptor.
 */

struct devif_callback_s;  /* Forward reference */

struct socket
{
  uint8_t       s_domain;    /* IP domain */
  uint8_t       s_type;      /* Protocol type */
  uint8_t       s_proto;     /* Socket Protocol */
  FAR void     *s_conn;      /* Connection inherits from struct socket_conn_s */

  /* Socket interface */

  FAR const struct sock_intf_s *s_sockif;
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: net_initialize
 *
 * Description:
 *   This is called from the OS initialization logic at power-up reset in
 *   order to configure networking data structures.  This is called prior
 *   to platform-specific driver initialization so that the networking
 *   subsystem is prepared to deal with network driver initialization
 *   actions.
 *
 *   Actions performed in this initialization phase assume that base OS
 *   facilities such as semaphores are available but this logic cannot
 *   depend upon OS resources such as interrupts or timers which are not
 *   yet available.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void net_initialize(void);

/****************************************************************************
 * Name: net_ioctl_arglen
 *
 * Description:
 *   Calculate the ioctl argument buffer length.
 *
 * Input Parameters:
 *   domain   The socket domain
 *   cmd      The ioctl command
 *
 * Returned Value:
 *   The argument buffer length, or error code.
 *
 ****************************************************************************/

ssize_t net_ioctl_arglen(uint8_t domain, int cmd);

/****************************************************************************
 * Critical section management.
 *
 * Re-entrant mutex based locking of the network is supported:
 *
 *   net_lock()        - Locks the network via a re-entrant mutex.
 *   net_unlock()      - Unlocks the network.
 *   net_sem_wait()    - Like pthread_cond_wait() except releases the
 *                       network momentarily to wait on another semaphore.
 *   net_ioballoc()    - Like iob_alloc() except releases the network
 *                       momentarily to wait for an IOB to become
 *                       available.
 *
 ****************************************************************************/

/****************************************************************************
 * Name: net_lock
 *
 * Description:
 *   Take the network lock
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Zero (OK) is returned on success; a negated errno value is returned on
 *   failure (probably -ECANCELED).
 *
 ****************************************************************************/

int net_lock(void);

/****************************************************************************
 * Name: net_trylock
 *
 * Description:
 *   Try to take the network lock only when it is currently not locked.
 *   Otherwise, it locks the semaphore.  In either
 *   case, the call returns without blocking.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Zero (OK) is returned on success; a negated errno value is returned on
 *   failure (probably -EAGAIN).
 *
 ****************************************************************************/

int net_trylock(void);

/****************************************************************************
 * Name: net_unlock
 *
 * Description:
 *   Release the network lock.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void net_unlock(void);

/****************************************************************************
 * Name: net_sem_timedwait
 *
 * Description:
 *   Atomically wait for sem (or a timeout( while temporarily releasing
 *   the lock on the network.
 *
 *   Caution should be utilized.  Because the network lock is relinquished
 *   during the wait, there could changes in the network state that occur
 *   before the lock is recovered.  Your design should account for this
 *   possibility.
 *
 * Input Parameters:
 *   sem     - A reference to the semaphore to be taken.
 *   timeout - The relative time to wait until a timeout is declared.
 *
 * Returned Value:
 *   Zero (OK) is returned on success; a negated errno value is returned on
 *   any failure.
 *
 ****************************************************************************/

int net_sem_timedwait(FAR sem_t *sem, unsigned int timeout);

/****************************************************************************
 * Name: net_mutex_timedlock
 *
 * Description:
 *   Atomically wait for mutex (or a timeout) while temporarily releasing
 *   the lock on the network.
 *
 *   Caution should be utilized.  Because the network lock is relinquished
 *   during the wait, there could be changes in the network state that occur
 *   before the lock is recovered.  Your design should account for this
 *   possibility.
 *
 * Input Parameters:
 *   mutex   - A reference to the mutex to be taken.
 *   timeout - The relative time to wait until a timeout is declared.
 *
 * Returned Value:
 *   Zero (OK) is returned on success; a negated errno value is returned on
 *   any failure.
 *
 ****************************************************************************/

int net_mutex_timedlock(FAR mutex_t *mutex, unsigned int timeout);

/****************************************************************************
 * Name: net_sem_wait
 *
 * Description:
 *   Atomically wait for sem while temporarily releasing the network lock.
 *
 *   Caution should be utilized.  Because the network lock is relinquished
 *   during the wait, there could changes in the network state that occur
 *   before the lock is recovered.  Your design should account for this
 *   possibility.
 *
 * Input Parameters:
 *   sem - A reference to the semaphore to be taken.
 *
 * Returned Value:
 *   Zero (OK) is returned on success; a negated errno value is returned on
 *   any failure.
 *
 ****************************************************************************/

int net_sem_wait(FAR sem_t *sem);

/****************************************************************************
 * Name: net_mutex_lock
 *
 * Description:
 *   Atomically wait for mutex while temporarily releasing the network lock.
 *
 *   Caution should be utilized.  Because the network lock is relinquished
 *   during the wait, there could be changes in the network state that occur
 *   before the lock is recovered.  Your design should account for this
 *   possibility.
 *
 * Input Parameters:
 *   mutex - A reference to the mutex to be taken.
 *
 * Returned Value:
 *   Zero (OK) is returned on success; a negated errno value is returned on
 *   any failure.
 *
 ****************************************************************************/

int net_mutex_lock(FAR mutex_t *mutex);

/****************************************************************************
 * Name: net_sem_timedwait_uninterruptible
 *
 * Description:
 *   This function is wrapped version of net_sem_timedwait(), which is
 *   uninterruptible and convenient for use.
 *
 * Input Parameters:
 *   sem     - A reference to the semaphore to be taken.
 *   timeout - The relative time to wait until a timeout is declared.
 *
 * Returned Value:
 *   Zero (OK) is returned on success; a negated errno value is returned on
 *   any failure.
 *
 ****************************************************************************/

int net_sem_timedwait_uninterruptible(FAR sem_t *sem, unsigned int timeout);

/****************************************************************************
 * Name: net_sem_wait_uninterruptible
 *
 * Description:
 *   This function is wrapped version of net_sem_wait(), which is
 *   uninterruptible and convenient for use.
 *
 * Input Parameters:
 *   sem - A reference to the semaphore to be taken.
 *
 * Returned Value:
 *   Zero (OK) is returned on success; a negated errno value is returned on
 *   any failure.
 *
 ****************************************************************************/

int net_sem_wait_uninterruptible(FAR sem_t *sem);

#ifdef CONFIG_MM_IOB

/****************************************************************************
 * Name: net_iobtimedalloc
 *
 * Description:
 *   Allocate an IOB.  If no IOBs are available, then atomically wait for
 *   for the IOB while temporarily releasing the lock on the network.
 *   This function is wrapped version of net_ioballoc(), this wait will
 *   be terminated when the specified timeout expires.
 *
 *   Caution should be utilized.  Because the network lock is relinquished
 *   during the wait, there could be changes in the network state that occur
 *   before the lock is recovered.  Your design should account for this
 *   possibility.
 *
 * Input Parameters:
 *   throttled  - An indication of the IOB allocation is "throttled"
 *   timeout    - The relative time to wait until a timeout is declared.
 *
 * Returned Value:
 *   A pointer to the newly allocated IOB is returned on success.  NULL is
 *   returned on any allocation failure.
 *
 ****************************************************************************/

FAR struct iob_s *net_iobtimedalloc(bool throttled, unsigned int timeout);

/****************************************************************************
 * Name: net_ioballoc
 *
 * Description:
 *   Allocate an IOB.  If no IOBs are available, then atomically wait for
 *   for the IOB while temporarily releasing the lock on the network.
 *
 *   Caution should be utilized.  Because the network lock is relinquished
 *   during the wait, there could changes in the network state that occur
 *   before the lock is recovered.  Your design should account for this
 *   possibility.
 *
 * Input Parameters:
 *   throttled  - An indication of the IOB allocation is "throttled"
 *
 * Returned Value:
 *   A pointer to the newly allocated IOB is returned on success.  NULL is
 *   returned on any allocation failure.
 *
 ****************************************************************************/

FAR struct iob_s *net_ioballoc(bool throttled);
#endif

/****************************************************************************
 * Name: sockfd_allocate
 *
 * Description:
 *   Allocate a socket descriptor
 *
 * Input Parameters:
 *   psock    A pointer to socket structure.
 *   oflags   Open mode flags.
 *
 * Returned Value:
 *   Allocate a struct files instance and associate it with an socket
 *   instance.  Returns the file descriptor == index into the files array.
 *
 ****************************************************************************/

int sockfd_allocate(FAR struct socket *psock, int oflags);

/****************************************************************************
 * Name: sockfd_socket
 *
 * Description:
 *   Given a socket descriptor, return the underlying socket structure.
 *
 * Input Parameters:
 *   sockfd - The socket descriptor index to use.
 *
 * Returns zero (OK) on success.  On failure, it returns a negated errno
 * value to indicate the nature of the error.
 *
 *    EBADF
 *      The file descriptor is not a valid index in the descriptor table.
 *    ENOTSOCK
 *      psock is a descriptor for a file, not a socket.
 *
 ****************************************************************************/

FAR struct socket *file_socket(FAR struct file *filep);
int sockfd_socket(int sockfd, FAR struct file **filep,
                  FAR struct socket **socketp);

/****************************************************************************
 * Name: psock_socket
 *
 * Description:
 *   socket() creates an endpoint for communication and returns a socket
 *   structure.
 *
 * Input Parameters:
 *   domain   (see sys/socket.h)
 *   type     (see sys/socket.h)
 *   protocol (see sys/socket.h)
 *   psock    A pointer to a user allocated socket structure to be
 *            initialized.
 *
 * Returned Value:
 *  Returns zero (OK) on success.  On failure, it returns a negated errno
 *  value to indicate the nature of the error:
 *
 *   EACCES
 *     Permission to create a socket of the specified type and/or protocol
 *     is denied.
 *   EAFNOSUPPORT
 *     The implementation does not support the specified address family.
 *   EINVAL
 *     Unknown protocol, or protocol family not available.
 *   EMFILE
 *     Process file table overflow.
 *   ENFILE
 *     The system limit on the total number of open files has been reached.
 *   ENOBUFS or ENOMEM
 *     Insufficient memory is available. The socket cannot be created until
 *     sufficient resources are freed.
 *   EPROTONOSUPPORT
 *     The protocol type or the specified protocol is not supported within
 *     this domain.
 *
 ****************************************************************************/

int psock_socket(int domain, int type, int protocol,
                 FAR struct socket *psock);

/****************************************************************************
 * Name: psock_close
 *
 * Description:
 *   Performs the close operation on a socket instance
 *
 * Input Parameters:
 *   psock   Socket instance
 *
 * Returned Value:
 *  Returns zero (OK) on success.  On failure, it returns a negated errno
 *  value to indicate the nature of the error.
 *
 ****************************************************************************/

int psock_close(FAR struct socket *psock);

/****************************************************************************
 * Name: psock_bind
 *
 * Description:
 *   bind() gives the socket 'psock' the local address 'addr'. 'addr' is
 *   'addrlen' bytes long. Traditionally, this is called "assigning a name to
 *   a socket." When a socket is created with socket, it exists in a name
 *   space (address family) but has no name assigned.
 *
 * Input Parameters:
 *   psock    Socket structure of the socket to bind
 *   addr     Socket local address
 *   addrlen  Length of 'addr'
 *
 * Returned Value:
 *  Returns zero (OK) on success.  On failure, it returns a negated errno
 *  value to indicate the nature of the error.
 *
 *   EACCES
 *     The address is protected, and the user is not the superuser.
 *   EADDRINUSE
 *     The given address is already in use.
 *   EINVAL
 *     The socket is already bound to an address.
 *   ENOTSOCK
 *     psock is a descriptor for a file, not a socket.
 *
 ****************************************************************************/

struct sockaddr; /* Forward reference. See nuttx/include/sys/socket.h */

int psock_bind(FAR struct socket *psock, FAR const struct sockaddr *addr,
               socklen_t addrlen);

/****************************************************************************
 * Name: psock_listen
 *
 * Description:
 *   To accept connections, a socket is first created with psock_socket(), a
 *   willingness to accept incoming connections and a queue limit for
 *   incoming connections are specified with psock_listen(), and then the
 *   connections are accepted with psock_accept(). The psock_listen() call
 *   applies only to sockets of type SOCK_STREAM or SOCK_SEQPACKET.
 *
 * Input Parameters:
 *   psock    Reference to an internal, boound socket structure.
 *   backlog  The maximum length the queue of pending connections may grow.
 *            If a connection request arrives with the queue full, the client
 *            may receive an error with an indication of ECONNREFUSED or,
 *            if the underlying protocol supports retransmission, the request
 *            may be ignored so that retries succeed.
 *
 * Returned Value:
 *  Returns zero (OK) on success.  On failure, it returns a negated errno
 *  value to indicate the nature of the error.
 *
 *   EADDRINUSE
 *     Another socket is already listening on the same port.
 *   EOPNOTSUPP
 *     The socket is not of a type that supports the listen operation.
 *
 ****************************************************************************/

int psock_listen(FAR struct socket *psock, int backlog);

/****************************************************************************
 * Name: psock_accept
 *
 * Description:
 *   The psock_accept function is used with connection-based socket types
 *   (SOCK_STREAM, SOCK_SEQPACKET and SOCK_RDM). It extracts the first
 *   connection request on the queue of pending connections, creates a new
 *   connected socket with mostly the same properties as 'sockfd', and
 *   allocates a new socket descriptor for the socket, which is returned. The
 *   newly created socket is no longer in the listening state. The original
 *   socket 'sockfd' is unaffected by this call.  Per file descriptor flags
 *   are not inherited across an psock_accept.
 *
 *   The 'sockfd' argument is a socket descriptor that has been created with
 *   socket(), bound to a local address with bind(), and is listening for
 *   connections after a call to listen().
 *
 *   On return, the 'addr' structure is filled in with the address of the
 *   connecting entity. The 'addrlen' argument initially contains the size
 *   of the structure pointed to by 'addr'; on return it will contain the
 *   actual length of the address returned.
 *
 *   If no pending connections are present on the queue, and the socket is
 *   not marked as non-blocking, psock_accept blocks the caller until a
 *   connection is present. If the socket is marked non-blocking and no
 *   pending connections are present on the queue, psock_accept returns
 *   EAGAIN.
 *
 * Input Parameters:
 *   psock    Reference to the listening socket structure
 *   addr     Receives the address of the connecting client
 *   addrlen  Input: allocated size of 'addr', Return: returned size of
 *            'addr'
 *   newsock  Location to return the accepted socket information.
 *   flags    The flags used for initialization
 *
 * Returned Value:
 *  Returns zero (OK) on success.  On failure, it returns a negated errno
 *  value to indicate the nature of the error.
 *
 * EAGAIN or EWOULDBLOCK
 *   The socket is marked non-blocking and no connections are present to
 *   be accepted.
 * EOPNOTSUPP
 *   The referenced socket is not of type SOCK_STREAM.
 * EINTR
 *   The system call was interrupted by a signal that was caught before
 *   a valid connection arrived.
 * ECONNABORTED
 *   A connection has been aborted.
 * EINVAL
 *   Socket is not listening for connections.
 * EMFILE
 *   The per-process limit of open file descriptors has been reached.
 * ENFILE
 *   The system maximum for file descriptors has been reached.
 * EFAULT
 *   The addr parameter is not in a writable part of the user address
 *   space.
 * ENOBUFS or ENOMEM
 *   Not enough free memory.
 * EPROTO
 *   Protocol error.
 * EPERM
 *   Firewall rules forbid connection.
 *
 ****************************************************************************/

int psock_accept(FAR struct socket *psock, FAR struct sockaddr *addr,
                 FAR socklen_t *addrlen, FAR struct socket *newsock,
                 int flags);

/****************************************************************************
 * Name: psock_connect
 *
 * Description:
 *   connect() connects the socket referred to by the structure 'psock'
 *   to the address specified by 'addr'. The addrlen argument specifies
 *   the size of 'addr'.  The format of the address in 'addr' is
 *   determined by the address space of the socket 'psock'.
 *
 *   If the socket 'psock' is of type SOCK_DGRAM then 'addr' is the address
 *   to which datagrams are sent by default, and the only address from which
 *   datagrams are received. If the socket is of type SOCK_STREAM or
 *   SOCK_SEQPACKET, this call attempts to make a connection to the socket
 *   that is bound to the address specified by 'addr'.
 *
 *   Generally, connection-based protocol sockets may successfully connect()
 *   only once; connectionless protocol sockets may use connect() multiple
 *   times to change their association.  Connectionless sockets may dissolve
 *   the association by connecting to an address with the sa_family member of
 *   sockaddr set to AF_UNSPEC.
 *
 * Input Parameters:
 *   psock     Pointer to a socket structure initialized by psock_socket()
 *   addr      Server address (form depends on type of socket)
 *   addrlen   Length of actual 'addr'
 *
 * Returned Value:
 *  Returns zero (OK) on success.  On failure, it returns a negated errno
 *  value to indicate the nature of the error.
 *
 *     EACCES, EPERM
 *       The user tried to connect to a broadcast address without having the
 *       socket broadcast flag enabled or the connection request failed
 *       because of a local firewall rule.
 *     EADDRINUSE
 *       Local address is already in use.
 *     EAFNOSUPPORT
 *       The passed address didn't have the correct address family in its
 *       sa_family field.
 *     EAGAIN
 *       No more free local ports or insufficient entries in the routing
 *       cache.
 *     EALREADY
 *       The socket is non-blocking and a previous connection attempt has
 *       not yet been completed.
 *     EBADF
 *       The file descriptor is not a valid index in the descriptor table.
 *     ECONNREFUSED
 *       No one listening on the remote address.
 *     EFAULT
 *       The socket structure address is outside the user's address space.
 *     EINPROGRESS
 *       The socket is non-blocking and the connection cannot be completed
 *       immediately.
 *     EINTR
 *       The system call was interrupted by a signal that was caught.
 *     EISCONN
 *       The socket is already connected.
 *     ENETUNREACH
 *       Network is unreachable.
 *     ENOTSOCK
 *       The file descriptor is not associated with a socket.
 *     ETIMEDOUT
 *       Timeout while attempting connection. The server may be too busy
 *       to accept new connections.
 *
 ****************************************************************************/

int psock_connect(FAR struct socket *psock, FAR const struct sockaddr *addr,
                  socklen_t addrlen);

/****************************************************************************
 * Name: psock_sendmsg
 *
 * Description:
 *   psock_sendmsg() sends messages to a socket, and may be used to
 *   send data on a socket whether or not it is connection-oriented.
 *   This is an internal OS interface. It is functionally equivalent to
 *   sendmsg() except that:
 *
 *   - It is not a cancellation point,
 *   - It does not modify the errno variable, and
 *   - I accepts the internal socket structure as an input rather than an
 *     task-specific socket descriptor.
 *
 * Input Parameters:
 *   psock     A pointer to a NuttX-specific, internal socket structure
 *   msg       Message to send
 *   flags     Send flags
 *
 * Returned Value:
 *   On success, returns the number of characters sent. Otherwise, on any
 *   failure, a negated errno value is returned (see comments with sendmsg()
 *   for a list of appropriate errno values).
 *
 ****************************************************************************/

ssize_t psock_sendmsg(FAR struct socket *psock, FAR struct msghdr *msg,
                      int flags);

/****************************************************************************
 * Name: psock_recvmsg
 *
 * Description:
 *   psock_recvmsg() receives messages from a socket, and may be used to
 *   receive data on a socket whether or not it is connection-oriented.
 *   This is an internal OS interface.  It is functionally equivalent to
 *   recvmsg() except that:
 *
 *   - It is not a cancellation point,
 *   - It does not modify the errno variable, and
 *   - It accepts the internal socket structure as an input rather than an
 *     task-specific socket descriptor.
 *
 * Input Parameters:
 *   psock     A pointer to a NuttX-specific, internal socket structure
 *   msg       Buffer to receive data
 *   flags     Receive flags
 *
 * Returned Value:
 *   On success, returns the number of characters received.  If no data is
 *   available to be received and the peer has performed an orderly shutdown,
 *   recvmsg() will return 0.  Otherwise, on any failure, a negated errno
 *   value is returned (see comments with recvmsg() for a list of appropriate
 *   errno values).
 *
 ****************************************************************************/

ssize_t psock_recvmsg(FAR struct socket *psock, FAR struct msghdr *msg,
                      int flags);

/****************************************************************************
 * Name: psock_send
 *
 * Description:
 *   The psock_send() call may be used only when the socket is in a
 *   connected state (so that the intended recipient is known).  This is an
 *   internal OS interface.  It is functionally equivalent to send() except
 *   that:
 *
 *   - It is not a cancellation point,
 *   - It does not modify the errno variable, and
 *   - I accepts the internal socket structure as an input rather than an
 *     task-specific socket descriptor.
 *
 *   See comments with send() for more a more complete description of the
 *   functionality.
 *
 * Input Parameters:
 *   psock   An instance of the internal socket structure.
 *   buf     Data to send
 *   len     Length of data to send
 *   flags   Send flags
 *
 * Returned Value:
 *   On success, returns the number of characters sent.  On any failure, a
 *   negated errno value is returned (See comments with send() for a list
 *   of the appropriate errno value).
 *
 ****************************************************************************/

ssize_t psock_send(FAR struct socket *psock, FAR const void *buf, size_t len,
                   int flags);

/****************************************************************************
 * Name: psock_sendto
 *
 * Description:
 *   If sendto() is used on a connection-mode (SOCK_STREAM, SOCK_SEQPACKET)
 *   socket, the parameters to and 'tolen' are ignored (and the error EISCONN
 *   may be returned when they are not NULL and 0), and the error ENOTCONN is
 *   returned when the socket was not actually connected.
 *
 * Input Parameters:
 *   psock    A pointer to a NuttX-specific, internal socket structure
 *   buf      Data to send
 *   len      Length of data to send
 *   flags    Send flags
 *   to       Address of recipient
 *   tolen    The length of the address structure
 *
 * Returned Value:
 *   On success, returns the number of characters sent.  On any failure, a
 *   negated errno value is returned.  One of:
 *
 *   EAGAIN or EWOULDBLOCK
 *     The socket is marked non-blocking and the requested operation
 *     would block.
 *   EBADF
 *     An invalid descriptor was specified.
 *   ECONNRESET
 *     Connection reset by peer.
 *   EDESTADDRREQ
 *     The socket is not connection-mode, and no peer address is set.
 *   EFAULT
 *      An invalid user space address was specified for a parameter.
 *   EINTR
 *      A signal occurred before any data was transmitted.
 *   EINVAL
 *      Invalid argument passed.
 *   EISCONN
 *     The connection-mode socket was connected already but a recipient
 *     was specified. (Now either this error is returned, or the recipient
 *     specification is ignored.)
 *   EMSGSIZE
 *     The socket type requires that message be sent atomically, and the
 *     size of the message to be sent made this impossible.
 *   ENOBUFS
 *     The output queue for a network interface was full. This generally
 *     indicates that the interface has stopped sending, but may be
 *     caused by transient congestion.
 *   ENOMEM
 *     No memory available.
 *   ENOTCONN
 *     The socket is not connected, and no target has been given.
 *   ENOTSOCK
 *     The argument s is not a socket.
 *   EOPNOTSUPP
 *     Some bit in the flags argument is inappropriate for the socket
 *     type.
 *   EPIPE
 *     The local end has been shut down on a connection oriented socket.
 *     In this case the process will also receive a SIGPIPE unless
 *     MSG_NOSIGNAL is set.
 *
 ****************************************************************************/

ssize_t psock_sendto(FAR struct socket *psock, FAR const void *buf,
                     size_t len, int flags, FAR const struct sockaddr *to,
                     socklen_t tolen);

/****************************************************************************
 * Name: psock_recvfrom
 *
 * Description:
 *   psock_recvfrom() receives messages from a socket, and may be used to
 *   receive data on a socket whether or not it is connection-oriented.
 *   This is an internal OS interface.  It is functionally equivalent to
 *   recvfrom() except that:
 *
 *   - It is not a cancellation point,
 *   - It does not modify the errno variable, and
 *   - It accepts the internal socket structure as an input rather than an
 *     task-specific socket descriptor.
 *
 * Input Parameters:
 *   psock     A pointer to a NuttX-specific, internal socket structure
 *   buf       Buffer to receive data
 *   len       Length of buffer
 *   flags     Receive flags
 *   from      Address of source (may be NULL)
 *   fromlen   The length of the address structure
 *
 * Returned Value:
 *   On success, returns the number of characters received.  If no data is
 *   available to be received and the peer has performed an orderly shutdown,
 *   psock_recvfrom() will return 0.  Otherwise, on any failure, a negated
 *   errno value is returned (see comments with recvfrom() for a list of
 *   appropriate errno values).
 *
 ****************************************************************************/

ssize_t psock_recvfrom(FAR struct socket *psock, FAR void *buf, size_t len,
                       int flags, FAR struct sockaddr *from,
                       FAR socklen_t *fromlen);

/* recv using the underlying socket structure */

#define psock_recv(psock,buf,len,flags) \
  psock_recvfrom(psock,buf,len,flags,NULL,0)

/****************************************************************************
 * Name: psock_getsockopt
 *
 * Description:
 *   getsockopt() retrieve the value for the option specified by the
 *   'option' argument for the socket specified by the 'psock' argument. If
 *   the size of the option value is greater than 'value_len', the value
 *   stored in the object pointed to by the 'value' argument will be silently
 *   truncated. Otherwise, the length pointed to by the 'value_len' argument
 *   will be modified to indicate the actual length of the 'value'.
 *
 *   The 'level' argument specifies the protocol level of the option. To
 *   retrieve options at the socket level, specify the level argument as
 *   SOL_SOCKET.
 *
 *   See <sys/socket.h> a complete list of values for the 'option' argument.
 *
 * Input Parameters:
 *   psock     Socket structure of the socket to query
 *   level     Protocol level to set the option
 *   option    identifies the option to get
 *   value     Points to the argument value
 *   value_len The length of the argument value
 *
 * Returned Value:
 *  Returns zero (OK) on success.  On failure, it returns a negated errno
 *  value to indicate the nature of the error:
 *
 *  EINVAL
 *    The specified option is invalid at the specified socket 'level' or the
 *    socket has been shutdown.
 *  ENOPROTOOPT
 *    The 'option' is not supported by the protocol.
 *  ENOTSOCK
 *    The 'psock' argument does not refer to a socket.
 *  ENOBUFS
 *    Insufficient resources are available in the system to complete the
 *    call.
 *
 ****************************************************************************/

int psock_getsockopt(FAR struct socket *psock, int level, int option,
                     FAR void *value, FAR socklen_t *value_len);

/****************************************************************************
 * Name: psock_setsockopt
 *
 * Description:
 *   psock_setsockopt() sets the option specified by the 'option' argument,
 *   at the protocol level specified by the 'level' argument, to the value
 *   pointed to by the 'value' argument for the socket on the 'psock'
 *   argument.
 *
 *   The 'level' argument specifies the protocol level of the option. To set
 *   options at the socket level, specify the level argument as SOL_SOCKET.
 *
 *   See <sys/socket.h> a complete list of values for the 'option' argument.
 *
 * Input Parameters:
 *   psock     Socket structure of socket to operate on
 *   level     Protocol level to set the option
 *   option    identifies the option to set
 *   value     Points to the argument value
 *   value_len The length of the argument value
 *
 * Returned Value:
 *  Returns zero (OK) on success.  On failure, it returns a negated errno
 *  value to indicate the nature of the error:
 *
 *  EDOM
 *    The send and receive timeout values are too big to fit into the
 *    timeout fields in the socket structure.
 *  EINVAL
 *    The specified option is invalid at the specified socket 'level' or the
 *    socket has been shut down.
 *  EISCONN
 *    The socket is already connected, and a specified option cannot be set
 *    while the socket is connected.
 *  ENOPROTOOPT
 *    The 'option' is not supported by the protocol.
 *  ENOTSOCK
 *    The 'sockfd' argument does not refer to a socket.
 *  ENOMEM
 *    There was insufficient memory available for the operation to complete.
 *  ENOBUFS
 *    Insufficient resources are available in the system to complete the
 *    call.
 *
 ****************************************************************************/

int psock_setsockopt(FAR struct socket *psock, int level, int option,
                     FAR const void *value, socklen_t value_len);

/****************************************************************************
 * Name: psock_getsockname
 *
 * Description:
 *   The psock_getsockname() function retrieves the locally-bound name of the
 *   the specified socket, stores this address in the sockaddr structure
 *   pointed to by the 'addr' argument, and stores the length of this
 *   address in the object pointed to by the 'addrlen' argument.
 *
 *   If the actual length of the address is greater than the length of the
 *   supplied sockaddr structure, the stored address will be truncated.
 *
 *   If the socket has not been bound to a local name, the value stored in
 *   the object pointed to by address is unspecified.
 *
 * Parameters:
 *   psock    Socket structure of socket to operate on
 *   addr     sockaddr structure to receive data [out]
 *   addrlen  Length of sockaddr structure [in/out]
 *
 * Returned Value:
 *   On success, 0 is returned, the 'addr' argument points to the address
 *   of the socket, and the 'addrlen' argument points to the length of the
 *   address. Otherwise, -1 is returned and errno is set to indicate the
 *   error.  Possible errno values that may be returned include:
 *
 *   EBADF      - The socket argument is not a valid file descriptor.
 *   ENOTSOCK   - The socket argument does not refer to a socket.
 *   EOPNOTSUPP - The operation is not supported for this socket's protocol.
 *   ENOTCONN   - The socket is not connected or otherwise has not had the
 *                peer pre-specified.
 *   EINVAL     - The socket has been shut down.
 *   ENOBUFS    - Insufficient resources were available in the system to
 *                complete the function.
 *
 ****************************************************************************/

int psock_getsockname(FAR struct socket *psock, FAR struct sockaddr *addr,
                      FAR socklen_t *addrlen);

/****************************************************************************
 * Name: psock_getpeername
 *
 * Description:
 *   The psock_getpeername() function retrieves the remote-connected name of
 *   the specified socket, stores this address in the sockaddr structure
 *   pointed to by the 'addr' argument, and stores the length of this address
 *   in the object pointed to by the 'addrlen' argument.
 *
 *   If the actual length of the address is greater than the length of the
 *   supplied sockaddr structure, the stored address will be truncated.
 *
 *   If the socket has not been bound to a local name, the value stored in
 *   the object pointed to by address is unspecified.
 *
 * Parameters:
 *   psock    Socket structure of socket to operate on
 *   addr     sockaddr structure to receive data [out]
 *   addrlen  Length of sockaddr structure [in/out]
 *
 * Returned Value:
 *   On success, 0 is returned, the 'addr' argument points to the address
 *   of the socket, and the 'addrlen' argument points to the length of the
 *   address. Otherwise, -1 is returned and errno is set to indicate the
 *   error.  Possible errno values that may be returned include:
 *
 *   EBADF      - The socket argument is not a valid file descriptor.
 *   ENOTSOCK   - The socket argument does not refer to a socket.
 *   EOPNOTSUPP - The operation is not supported for this socket's protocol.
 *   ENOTCONN   - The socket is not connected or otherwise has not had the
 *                peer pre-specified.
 *   EINVAL     - The socket has been shut down.
 *   ENOBUFS    - Insufficient resources were available in the system to
 *                complete the function.
 *
 ****************************************************************************/

int psock_getpeername(FAR struct socket *psock, FAR struct sockaddr *addr,
                      FAR socklen_t *addrlen);

/****************************************************************************
 * Name: psock_ioctl and psock_vioctl
 *
 * Description:
 *   Perform network device specific operations.
 *
 * Input Parameters:
 *   psock    A pointer to a NuttX-specific, internal socket structure.
 *   cmd      The ioctl command.
 *   ap      The argument of the ioctl cmd.
 *
 * Returned Value:
 *   A non-negative value is returned on success; a negated errno value is
 *   returned on any failure to indicate the nature of the failure:
 *
 *   EBADF
 *     'psock' is not a valid, connected socket structure.
 *   EFAULT
 *     'arg' references an inaccessible memory area.
 *   ENOTTY
 *     'cmd' not valid.
 *   EINVAL
 *     'arg' is not valid.
 *   ENOTTY
 *     'sockfd' is not associated with a network device.
 *   ENOTTY
 *      The specified request does not apply to the kind of object that the
 *      descriptor 'sockfd' references.
 *
 ****************************************************************************/

int psock_vioctl(FAR struct socket *psock, int cmd, va_list ap);
int psock_ioctl(FAR struct socket *psock, int cmd, ...);

/****************************************************************************
 * Name: psock_shutdown
 *
 * Description:
 *   The shutdown() function will cause all or part of a full-duplex
 *   connection on the socket associated with the file descriptor socket to
 *   be shut down.
 *
 *   The shutdown() function disables subsequent send and/or receive
 *   operations on a socket, depending on the value of the how argument.
 *
 * Input Parameters:
 *   sockfd - Specifies the file descriptor of the socket.
 *   how    - Specifies the type of shutdown. The values are as follows:
 *
 *     SHUT_RD   - Disables further receive operations.
 *     SHUT_WR   - Disables further send operations.
 *     SHUT_RDWR - Disables further send and receive operations.
 *
 * Returned Value:
 *   On success, returns the number of characters sent.  On any failure, a
 *   negated errno value is returned.  One of:
 *
 *     EINVAL     - The how argument is invalid.
 *     ENOTCONN   - The socket is not connected.
 *     ENOTSOCK   - The socket argument does not refer to a socket.
 *     ENOBUFS    - Insufficient resources were available in the system to
 *                  perform the operation.
 *     EOPNOTSUPP - The operation is not supported for this socket's protocol
 *
 ****************************************************************************/

int psock_shutdown(FAR struct socket *psock, int how);

/****************************************************************************
 * Name: psock_poll
 *
 * Description:
 *   The standard poll() operation redirects operations on socket descriptors
 *   to this function.
 *
 * Input Parameters:
 *   psock - An instance of the internal socket structure.
 *   fds   - The structure describing the events to be monitored, OR NULL if
 *           this is a request to stop monitoring events.
 *   setup - true: Setup up the poll; false: Teardown the poll.
 *
 * Returned Value:
 *  0: Success; Negated errno on failure.
 *
 ****************************************************************************/

struct pollfd; /* Forward reference -- see poll.h */
int psock_poll(FAR struct socket *psock, struct pollfd *fds, bool setup);

/****************************************************************************
 * Name: psock_dup2
 *
 * Description:
 *   Clone a socket instance to a new instance.
 *
 * Returned Value:
 *   On success, returns the number of new socket.  On any error,
 *   a negated errno value is returned.
 *
 ****************************************************************************/

int psock_dup2(FAR struct socket *psock1, FAR struct socket *psock2);

/****************************************************************************
 * Name: psock_fstat
 *
 * Description:
 *   Performs fstat operations on socket.
 *
 * Input Parameters:
 *   psock  - An instance of the internal socket structure.
 *   buf    - Caller-provided location in which to return the fstat data.
 *
 * Returned Value:
 *   Zero (OK) is returned on success; a negated errno value is returned on
 *   any failure to indicate the nature of the failure.
 *
 ****************************************************************************/

int psock_fstat(FAR struct socket *psock, FAR struct stat *buf);

/****************************************************************************
 * Name: psock_sendfile
 *
 * Description:
 *   The send() call may be used only when the socket is in a connected state
 *   (so that the intended recipient is known). The only difference between
 *   send() and write() is the presence of flags. With zero flags parameter,
 *   send() is equivalent to write(). Also, send(sockfd,buf,len,flags) is
 *   equivalent to sendto(sockfd,buf,len,flags,NULL,0).
 *
 * Input Parameters:
 *   psock    An instance of the internal socket structure.
 *   buf      Data to send.
 *   len      Length of data to send.
 *   flags    Send flags.
 *
 * Returned Value:
 *   On success, returns the number of characters sent.  On  error,
 *   the negative errno is return appropriately:
 *
 *   EAGAIN or EWOULDBLOCK
 *     The socket is marked non-blocking and the requested operation
 *     would block.
 *   EBADF
 *     An invalid descriptor was specified.
 *   ECONNRESET
 *     Connection reset by peer.
 *   EDESTADDRREQ
 *     The socket is not connection-mode, and no peer address is set.
 *   EFAULT
 *      An invalid user space address was specified for a parameter.
 *   EINTR
 *      A signal occurred before any data was transmitted.
 *   EINVAL
 *      Invalid argument passed.
 *   EISCONN
 *     The connection-mode socket was connected already but a recipient
 *     was specified. (Now either this error is returned, or the recipient
 *     specification is ignored.)
 *   EMSGSIZE
 *     The socket type requires that message be sent atomically, and the
 *     size of the message to be sent made this impossible.
 *   ENOBUFS
 *     The output queue for a network interface was full. This generally
 *     indicates that the interface has stopped sending, but may be
 *     caused by transient congestion.
 *   ENOMEM
 *     No memory available.
 *   ENOTCONN
 *     The socket is not connected, and no target has been given.
 *   ENOTSOCK
 *     The argument s is not a socket.
 *   EOPNOTSUPP
 *     Some bit in the flags argument is inappropriate for the socket
 *     type.
 *   EPIPE
 *     The local end has been shut down on a connection oriented socket.
 *     In this case the process will also receive a SIGPIPE unless
 *     MSG_NOSIGNAL is set.
 *
 ****************************************************************************/

#ifdef CONFIG_NET_SENDFILE
ssize_t psock_sendfile(FAR struct socket *psock, FAR struct file *infile,
                       FAR off_t *offset, size_t count);
#endif

/****************************************************************************
 * Name: psock_socketpair
 *
 * Description:
 * Create an unbound pair of connected sockets in a specified domain, of a
 * specified type, under the protocol optionally specified by the protocol
 * argument. The two sockets shall be identical. The file descriptors used
 * in referencing the created sockets shall be returned in
 * sv[0] and sv[1].
 *
 * Input Parameters:
 *   domain   (see sys/socket.h)
 *   type     (see sys/socket.h)
 *   protocol (see sys/socket.h)
 *   psocks   A pointer to a user allocated socket structure to be paired.
 *
 ****************************************************************************/

int psock_socketpair(int domain, int type, int protocol,
                     FAR struct socket *psocks[2]);

/****************************************************************************
 * Name: netdev_register
 *
 * Description:
 *   Register a network device driver and assign a name to it so that it can
 *   be found in subsequent network ioctl operations on the device.
 *
 *   A custom, device-specific interface name format string may be selected
 *   by putting that format string into the device structure's d_ifname[]
 *   array before calling netdev_register().  Otherwise, the d_ifname[] must
 *   be zeroed on entry.
 *
 * Input Parameters:
 *   dev    - The device driver structure to be registered.
 *   lltype - Link level protocol used by the driver (Ethernet, SLIP, TUN,
 *            ...
 *
 * Returned Value:
 *   0:Success; negated errno on failure.
 *
 * Assumptions:
 *   Called during system initialization from normal user mode.
 *
 ****************************************************************************/

struct net_driver_s; /* Forward reference */
int netdev_register(FAR struct net_driver_s *dev, enum net_lltype_e lltype);

/****************************************************************************
 * Name: netdev_unregister
 *
 * Description:
 *   Unregister a network device driver.
 *
 * Input Parameters:
 *   dev - The device driver structure to un-register.
 *
 * Returned Value:
 *   0:Success; negated errno on failure.
 *
 * Assumptions:
 *   Currently only called for USB networking devices when the device is
 *   physically removed from the slot.
 *
 ****************************************************************************/

int netdev_unregister(FAR struct net_driver_s *dev);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* CONFIG_NET */
#endif /* __INCLUDE_NUTTX_NET_NET_H */
