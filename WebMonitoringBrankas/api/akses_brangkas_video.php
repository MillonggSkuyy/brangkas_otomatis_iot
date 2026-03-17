<?php
require __DIR__ . '/../config/db.php';   // ini harus menghasilkan $pdo (PDO)
require __DIR__ . '/../config/app.php';  // optional kalau perlu BASE_URL

header('Content-Type: application/json');
date_default_timezone_set('Asia/Jakarta');

$aktivitas = $_POST['aktivitas'] ?? 'ESP32_CAM';

$fotoDir  = __DIR__ . '/../uploads/foto/';
$videoDir = __DIR__ . '/../uploads/video/';

if (!is_dir($fotoDir))  mkdir($fotoDir, 0777, true);
if (!is_dir($videoDir)) mkdir($videoDir, 0777, true);

function saveUpload(string $key, string $dir, string $prefix, string $defaultExt): ?string {
  if (!isset($_FILES[$key])) return null;
  if (!is_array($_FILES[$key]) || ($_FILES[$key]['error'] ?? UPLOAD_ERR_NO_FILE) !== UPLOAD_ERR_OK) return null;

  $ext = pathinfo($_FILES[$key]['name'], PATHINFO_EXTENSION);
  if ($ext === '') $ext = $defaultExt;

  $name = $prefix . '_' . uniqid('', true) . '.' . $ext;
  $path = $dir . $name;

  if (!move_uploaded_file($_FILES[$key]['tmp_name'], $path)) return null;
  return $name;
}

$foto  = saveUpload('photo', $fotoDir, 'f', 'jpg');  // WAJIB
$video = saveUpload('video', $videoDir, 'v', 'avi'); // opsional

if (!$foto) {
  http_response_code(400);
  echo json_encode(["ok" => false, "error" => "FOTO wajib (field 'photo')"]);
  exit;
}

try {
  $tanggal = date('Y-m-d');
  $jam     = date('H:i:s');

  // tabel kamu: akses(tanggal, jam, aktivitas, foto, video)
  $stmt = $pdo->prepare("INSERT INTO akses (tanggal, jam, aktivitas, foto, video) VALUES (?, ?, ?, ?, ?)");
  $stmt->execute([$tanggal, $jam, $aktivitas, $foto, $video]);

  echo json_encode([
    "ok" => true,
    "id" => $pdo->lastInsertId(),
    "foto" => $foto,
    "video" => $video
  ]);
} catch (Throwable $e) {
  http_response_code(500);
  echo json_encode(["ok" => false, "error" => "DB insert gagal", "detail" => $e->getMessage()]);
}
