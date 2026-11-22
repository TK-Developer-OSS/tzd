<?php
echo "a=", $a, " b=", $b, " len=", strlen($big), "\n";
if (isset($userfile)) echo "upload ", $userfile_name, " ", $userfile_size, " ", implode("", file($userfile)), "\n";
?>
