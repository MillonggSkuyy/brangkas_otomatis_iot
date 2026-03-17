<?php
require __DIR__ . '/../config/db.php';
header('Content-Type: application/json; charset=utf-8');

$API_FIELD = 'api_key';
$API_KEY   = 'MYBRANKAS_KEY_12345';

function jsonOut($code, $arr) {
  http_response_code($code);
  echo json_encode($arr, JSON_UNESCAPED_UNICODE);
  exit;
}

$api = $_POST[$API_FIELD] ?? ($_GET[$API_FIELD] ?? '');
if ($api !== $API_KEY) jsonOut(401, ['ok'=>false,'msg'=>'unauthorized']);

$pin = $pdo->query("SELECT pin_brangkas FROM settings WHERE id=1")->fetchColumn();

jsonOut(200, [
  'ok' => true,
  'pin' => (string)$pin,
  'db' => $pdo->query("SELECT DATABASE()")->fetchColumn(),
  'host' => $pdo->query("SELECT @@hostname")->fetchColumn(),
  'file' => __FILE__
]);
