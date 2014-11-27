make clean
make install
service apache2 restart
php -f tests/test.php
