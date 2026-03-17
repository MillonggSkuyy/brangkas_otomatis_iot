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
function isPin4($s) { return preg_match('/^\d{4}$/', $s) === 1; }

$api      = $_POST[$API_FIELD] ?? '';
$pin_lama = trim($_POST['pin_lama'] ?? '');
$pin_baru = trim($_POST['pin_baru'] ?? '');

if ($api !== $API_KEY) jsonOut(401, ['ok'=>false,'msg'=>'unauthorized']);
if (!isPin4($pin_baru)) jsonOut(400, ['ok'=>false,'msg'=>'pin_baru harus 4 digit angka']);

$currentPin = $pdo->query("SELECT pin_brangkas FROM settings WHERE id=1")->fetchColumn();
if ($currentPin === false) jsonOut(404, ['ok'=>false,'msg'=>'settings id=1 tidak ditemukan']);

if ($pin_lama !== '' && $pin_lama !== $currentPin) jsonOut(400, ['ok'=>false,'msg'=>'pin_lama salah']);

$stmt = $pdo->prepare("UPDATE settings SET pin_brangkas=?, updated_at=NOW() WHERE id=1");
$stmt->execute([$pin_baru]);

$after = $pdo->query("SELECT pin_brangkas FROM settings WHERE id=1")->fetchColumn();

jsonOut(200, [
  'ok' => true,
  'msg' => 'pin updated',
  'pin_current' => (string)$after,
  'db' => $pdo->query("SELECT DATABASE()")->fetchColumn(),
  'file' => __FILE__
]);
