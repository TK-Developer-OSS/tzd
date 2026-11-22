<?php
echo "PHP ", phpversion(), "\n";
$a = array(3, 1, 2); sort($a); echo implode(",", $a), "\n";
$h = array("x" => 1, "y" => 2); while (list($k, $v) = each($h)) echo "$k=$v ";
echo "\n", strtoupper("tzd"), " ", strrev("abc"), " ", str_replace("a", "b", "banana"), "\n";
echo md5("abc"), " ", base64_encode("TzD"), " ", soundex("Tzd"), "\n";
printf("%05d|%-6s|%.3f|%x\n", 42, "ab", 3.14159, 255);
$r = ""; for ($i = 0; $i < 1000; $i++) $r .= "x"; echo sprintf("%s", substr($r, 0, 5)), " ", strlen($r), "\n";
echo ereg_replace("[0-9]+", "#", "a12b345"), " ", preg_replace("/b+/", "X", "abbbc"), "\n";
$parts = explode(" ", "one two three"); echo count($parts), " ", $parts[2], "\n";
echo urlencode("a b&c"), " ", htmlspecialchars("<a>"), " ", nl2br("x\ny"), "\n";
class Counter { var $n = 0; function inc() { $this->n++; return $this->n; } }
$c = new Counter; $c->inc(); echo "class ", $c->inc(), "\n";
function fact($n) { return $n <= 1 ? 1 : $n * fact($n - 1); }
echo "fact ", fact(10), "\n";
echo "q=", $q, " n=", $n2, "\n";
?>
