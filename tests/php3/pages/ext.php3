<?php
$t = dirname($PATH_TRANSLATED) . "/tmp/t" . getmypid() . ".gz";
$f = gzopen($t, "w"); gzwrite($f, "hello gz\nline2\n"); gzclose($f);
$f = gzopen($t, "r"); echo gzgets($f, 100); echo gzread($f, 100); gzclose($f); unlink($t);
function se($p, $name, $attrs) { echo "<", $name, ">"; }
function ee($p, $name) { echo "</", $name, ">"; }
$x = xml_parser_create(); xml_set_element_handler($x, "se", "ee");
xml_parse($x, "<a><b>t</b><c/></a>", 1); xml_parser_free($x); echo "\n";
echo function_exists("ftp_connect") ? "ftp ok\n" : "ftp ng\n";
echo posix_getpid() > 0 ? "posix ok\n" : "posix ng\n";
$c = mysql_connect("127.0.0.1:13306", "tzd", "tzdpass");
mysql_select_db("tzd", $c);
mysql_query("delete from t where id > 2", $c);
mysql_query("insert into t values (3, '" . addslashes("o'k") . "')", $c);
$r = mysql_query("select id, name from t order by id", $c);
while ($row = mysql_fetch_array($r)) echo $row["id"], "=", $row["name"], "\n";
echo "rows ", mysql_num_rows($r), "\n";
mysql_close($c);
?>
