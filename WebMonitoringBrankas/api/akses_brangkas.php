<?php
require __DIR__ . '/../config/db.php';
header('Content-Type: application/json');

date_default_timezone_set('Asia/Jakarta');

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
  http_response_code(405);
  echo json_encode(['error' => 'Method not allowed']);
  exit;
}

// ===================== AUTH =====================
$API_KEY = "MYBRANKAS_KEY_12345";
$apiKey  = $_POST['api_key'] ?? '';
if ($apiKey !== $API_KEY) {
  http_response_code(401);
  echo json_encode(['error' => 'Unauthorized']);
  exit;
}

$mode = trim($_POST['mode'] ?? 'photo_only'); // photo_only | video_only

$uploadDirFoto  = __DIR__ . '/../uploads/foto/';
$uploadDirVideo = __DIR__ . '/../uploads/video/';

if (!is_dir($uploadDirFoto))  @mkdir($uploadDirFoto, 0777, true);
if (!is_dir($uploadDirVideo)) @mkdir($uploadDirVideo, 0777, true);

// ===================== HELPER =====================
function uploadErrText(int $code): string {
  $map = [
    UPLOAD_ERR_OK => 'UPLOAD_ERR_OK',
    UPLOAD_ERR_INI_SIZE => 'UPLOAD_ERR_INI_SIZE (upload_max_filesize)',
    UPLOAD_ERR_FORM_SIZE => 'UPLOAD_ERR_FORM_SIZE (MAX_FILE_SIZE)',
    UPLOAD_ERR_PARTIAL => 'UPLOAD_ERR_PARTIAL',
    UPLOAD_ERR_NO_FILE => 'UPLOAD_ERR_NO_FILE',
    UPLOAD_ERR_NO_TMP_DIR => 'UPLOAD_ERR_NO_TMP_DIR',
    UPLOAD_ERR_CANT_WRITE => 'UPLOAD_ERR_CANT_WRITE',
    UPLOAD_ERR_EXTENSION => 'UPLOAD_ERR_EXTENSION',
  ];
  return $map[$code] ?? 'UNKNOWN';
}

function saveFile(array $file, string $dir, array $allowExt): ?string {
  $err = $file['error'] ?? UPLOAD_ERR_NO_FILE;
  if ($err !== UPLOAD_ERR_OK) return null;

  if (empty($file['tmp_name']) || !is_uploaded_file($file['tmp_name'])) return null;

  $ext = strtolower(pathinfo($file['name'] ?? '', PATHINFO_EXTENSION));
  if ($ext === '') return null;
  if (!in_array($ext, $allowExt, true)) return null;

  $basename = uniqid('f_', true) . "." . $ext;
  $dest = $dir . $basename;

  if (!move_uploaded_file($file['tmp_name'], $dest)) return null;
  return $basename;
}

// ===================== MODE: PHOTO ONLY (INSERT) =====================
if ($mode === 'photo_only') {
  $aktivitas = trim($_POST['aktivitas'] ?? '');
  if ($aktivitas === '') {
    http_response_code(400);
    echo json_encode(['error' => 'aktivitas wajib']);
    exit;
  }

  if (!isset($_FILES['photo'])) {
    http_response_code(400);
    echo json_encode(['error' => "field file 'photo' tidak ada"]);
    exit;
  }

  // Debug upload foto
  if (($_FILES['photo']['error'] ?? UPLOAD_ERR_NO_FILE) !== UPLOAD_ERR_OK) {
    $e = (int)($_FILES['photo']['error'] ?? 0);
    http_response_code(400);
    echo json_encode([
      'error' => 'upload photo error',
      'upload_err_code' => $e,
      'upload_err_text' => uploadErrText($e),
      'size' => $_FILES['photo']['size'] ?? null
    ]);
    exit;
  }

  $photoName = saveFile($_FILES['photo'], $uploadDirFoto, ['jpg','jpeg','png']);
  if (!$photoName) {
    http_response_code(500);
    echo json_encode(['error' => 'Gagal simpan photo (cek ekstensi / permission folder)']);
    exit;
  }

  $stmt = $pdo->prepare('
    INSERT INTO akses (tanggal, jam, aktivitas, foto, video)
    VALUES (CURRENT_DATE(), CURRENT_TIME(), ?, ?, NULL)
  ');
  $stmt->execute([$aktivitas, $photoName]);
  $aksesId = (int)$pdo->lastInsertId();

  echo json_encode(['success' => true, 'id' => $aksesId, 'photo' => $photoName]);
  exit;
}

// ===================== MODE: VIDEO ONLY (UPDATE) =====================
if ($mode === 'video_only') {
  $aksesId = (int)($_POST['akses_id'] ?? 0);
  if ($aksesId <= 0) {
    http_response_code(400);
    echo json_encode(['error' => 'akses_id wajib untuk mode video_only']);
    exit;
  }

  if (!isset($_FILES['video'])) {
    http_response_code(400);
    echo json_encode(['error' => "field file 'video' tidak ada"]);
    exit;
  }

  // Debug upload video (INI YANG BIASANYA MENJELASKAN KENAPA GAGAL)
  if (($_FILES['video']['error'] ?? UPLOAD_ERR_NO_FILE) !== UPLOAD_ERR_OK) {
    $e = (int)($_FILES['video']['error'] ?? 0);
    http_response_code(400);
    echo json_encode([
      'error' => 'upload video error',
      'upload_err_code' => $e,
      'upload_err_text' => uploadErrText($e),
      'size' => $_FILES['video']['size'] ?? null,
      'hint' => 'Kalau code=1/2 berarti kepentok upload_max_filesize/post_max_size di php.ini'
    ]);
    exit;
  }

  // Simpan video (boleh avi saja, tapi aku longgarkan sedikit biar fleksibel)
  $videoName = saveFile($_FILES['video'], $uploadDirVideo, ['avi','mp4','mjpg','mjpeg']);
  if (!$videoName) {
    http_response_code(500);
    echo json_encode(['error' => 'Gagal simpan video (cek ekstensi / permission folder uploads/video)']);
    exit;
  }

  // pastikan id ada
  $cek = $pdo->prepare('SELECT id FROM akses WHERE id=? LIMIT 1');
  $cek->execute([$aksesId]);
  if (!$cek->fetchColumn()) {
    http_response_code(404);
    echo json_encode(['error' => 'akses_id tidak ditemukan']);
    exit;
  }

  $upd = $pdo->prepare('UPDATE akses SET video=? WHERE id=?');
  $upd->execute([$videoName, $aksesId]);

  echo json_encode(['success' => true, 'id' => $aksesId, 'video' => $videoName]);
  exit;
}

// ===================== MODE TIDAK DIKENAL =====================
http_response_code(400);
echo json_encode(['error' => 'mode tidak dikenal (pakai photo_only atau video_only)']);
exit;
