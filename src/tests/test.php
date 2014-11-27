<?php
$link = msgsrv_open('localhost', 6004, 'PHP', 'PHP', 'PHP');
if (!$link) {
	die('Could not connect: ' . msgsrv_last_error());
}

var_dump($link);

$link2 = msgsrv_open('localhost', 6004, 'PHP', 'PHP', 'PHP');
if (!$link2) {
	die('Could not connect: ' . msgsrv_last_error());
}
var_dump($link2);
echo "Connected successfully!\n";

if (msgsrv_send("dt", "Echo", "content", $link2) == FALSE) {
	echo 'message send failed! ' . msgsrv_last_error();
}
echo "message sent!\n";

if (msgsrv_receive(function($status, $source, $command, $content, $link) {
	echo $status, '', $source, $command, $content;
	var_dump($link);
}, 500) == FALSE) {
	echo 'message receive failed! ' . msgsrv_last_error();
}

if (msgsrv_receive(function($status, $source, $command, $content, $link) {
	echo $status, '', $source, $command, $content;
	var_dump($link);
}, 500) == FALSE) {
    echo 'message receive failed! ' . msgsrv_last_error();
}

if (($response = msgsrv_request ("root", "Echo", "content", $link)) == FALSE) {
	echo 'request failed! ' . msgsrv_last_error() ;
}
var_dump($response);


msgsrv_close($link);
msgsrv_close($link2);