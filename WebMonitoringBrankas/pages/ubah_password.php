<?php
require __DIR__ . '/../includes/auth_guard.php';
require __DIR__ . '/../config/db.php';

$alert = '';
$success = false;
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $pass_lama = $_POST['password_lama'] ?? '';
    $pass_baru = $_POST['password_baru'] ?? '';
    $pass_konf = $_POST['password_konfirmasi'] ?? '';

    if ($pass_baru !== $pass_konf) {
        $alert = 'Konfirmasi password tidak sama.';
    } else {
        $stmt = $pdo->prepare('SELECT password_hash FROM users WHERE id=?');
        $stmt->execute([$_SESSION['user']['id']]);
        $hash = $stmt->fetchColumn();

        if ($hash && password_verify($pass_lama, $hash)) {
            $newHash = password_hash($pass_baru, PASSWORD_DEFAULT);
            $upd = $pdo->prepare('UPDATE users SET password_hash=? WHERE id=?');
            $upd->execute([$newHash, $_SESSION['user']['id']]);
            $alert = 'Password berhasil diubah.';
            $success = true;
        } else {
            $alert = 'Password lama salah.';
        }
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
    <label>Masukkan Password Sebelumnya</label>
    <input type="password" name="password_lama" required>

    <label>Masukkan Password baru</label>
    <input type="password" name="password_baru" required>

    <label>Konfirmasi Password baru</label>
    <input type="password" name="password_konfirmasi" required>

    <div style="text-align:right; margin-top:12px;">
      <button type="submit">Ubah</button>
    </div>
  </form>
</div>
<?php include __DIR__ . '/../includes/footer.php'; ?>
