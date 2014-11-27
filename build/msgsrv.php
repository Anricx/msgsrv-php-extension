<?php

/**
 * Open a connection to a MsgSrv
 * @param string $host MsgSrv's hostname or ip address
 * @param int $port MsgSrv's port
 * @param string $appname appname login with 
 * @param string $username username login with
 * @param string $password password for login
 * @return resource a MsgSrv link identifier on success or false for failure.
 */
function msgsrv_open ($host, $port, $appname, $username, $password) {}

/**
 * Just send message to target, not waite target response.
 * @param string $target the targe app
 * @param string $cmd command of message
 * @param string $content content of message
 * @param resource $link send request throught this MsgSrv link
 * @return bool return true on success or false for failure.
 */
function msgsrv_send ($target, $cmd, $content, $link) {}

/**
 * Try to receive a message from remote.
 * @param function $callback when message were received or receive message timeout this callback function will be called!
 * @param int $timeout waite message timeout in ms.
 * @param resource $link optional, null means receive from any msgsrv's connection in current page.
 */
function msgsrv_receive($callback, $timeout = 30000 , $link = null) {}

/**
 * Send message to target and waite response until timeout
 * @param string $target the targe app
 * @param string $cmd command of message
 * @param string $content content of message
 * @param resource $link send request throught this MsgSrv link
 * @param int $timeout waite target response timeout in ms.
 * @return array return response message on success or false for failure.
 */
function msgsrv_request ($target, $cmd, $content, $link, $timeout = 30000) {}

/**
 * Get the last error code.
 * @return int the last error code define in msgsrv
 */
function msgsrv_last_error () {}

/**
 * Close link with MsgSrv
 * @param resource $link the MsgSrv link that need to be close. 
 */
function msgsrv_close ($link) {}

?>