make clean
make install
service php-fpm restart
service apache2 restart
php -f run.php
