<?php
require __DIR__ . '/../includes/auth_guard.php';
require __DIR__ . '/../config/db.php';

$alert = '';
$success = false;

function isPin4($s) {
  return preg_match('/^\d{4}$/', $s) === 1;
}

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $pin_lama = trim($_POST['pin_lama'] ?? '');
    $pin_baru = trim($_POST['pin_baru'] ?? '');

    $currentPin = $pdo->query('SELECT pin_brangkas FROM settings WHERE id=1')->fetchColumn();

    if (!isPin4($pin_lama) || !isPin4($pin_baru)) {
        $alert = 'PIN harus 4 digit angka';
    } elseif ($pin_lama !== $currentPin) {
        $alert = 'PIN lama salah';
    } elseif ($pin_baru === $currentPin) {
        $alert = 'PIN baru tidak boleh sama dengan PIN lama';
    } else {
        $stmt = $pdo->prepare('UPDATE settings SET pin_brangkas=?, updated_at=NOW() WHERE id=1');
        $stmt->execute([$pin_baru]);
        $alert = 'PIN berhasil diubah';
        $success = true;
    }
}

$pageTitle = 'Pengaturan';
include __DIR__ . '/../includes/header.php';
?>
<div style="max-width:760px;">
  <?php if ($alert): ?>
    <div class="alert <?= $success ? 'success' : 'error' ?>"><?= htmlspecialchars($alert) ?></div>
  <?php endif; ?>
  <form method="post">
    <label>Masukkan PIN Sebelumnya</label>
    <input name="pin_lama" required maxlength="4" inputmode="numeric" pattern="\d{4}" type="password">

    <label>Masukkan PIN baru</label>
    <input name="pin_baru" required maxlength="4" inputmode="numeric" pattern="\d{4}" type="password">

    <div style="text-align:right; margin-top:12px;">
      <button type="submit">Ubah</button>
    </div>
  </form>
</div>
<?php include __DIR__ . '/../includes/footer.php'; ?>
