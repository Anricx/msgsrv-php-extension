<?php

$link = msgsrv_open('10.170.33.111', 6004, 'PHP', 'PHP', 'PHP', 1);
// var_dump($link);
// $link = msgsrv_pconnect('10.2.10.201', 6008, 'PHP', 'PHP', 'PHP');
// var_dump($link);

$flag = msgsrv_send('LivingServices', 'Discover', 'v=1.0 s= i=10.2.70.213 n=4304512852397275 u=1421211964000 f=5bc676e00eba46a4398965fcfa31bbd5 t=json d=eyJsb2NhdGlvbiI6IjMwLjU0NjQ4MywxMDQuMDc5NDk5IiwibG9jYWxlIjoiemgtQ04iLCJ2ZWhpY2xlX2xpbWl0cyI6IjEiLCJ3ZWF0aGVyIjoiMSJ9', $link);
//var_dump($flag);

$flag = msgsrv_receive(function($status, $from, $cmd, $body, $link) {
    echo $from, ' ', $cmd , ' ', $body, "\n";
}, 3, 1);

// $flag = msgsrv_request('LivingServices', 'Discover', 'v=1.0 s= i=10.2.70.213 n=4304512852397275 u=1421211964000 f=5bc676e00eba46a4398965fcfa31bbd5 t=json d=eyJsb2NhdGlvbiI6IjMwLjU0NjQ4MywxMDQuMDc5NDk5IiwibG9jYWxlIjoiemgtQ04iLCJ2ZWhpY2xlX2xpbWl0cyI6IjEiLCJ3ZWF0aGVyIjoiMSJ9', $link);

echo msgsrv_full_app($link), "\n";
