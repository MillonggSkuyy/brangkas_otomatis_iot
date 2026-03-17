<?php
require __DIR__ . '/../includes/auth_guard.php';
require __DIR__ . '/../config/app.php';
$baseUrl = rtrim(BASE_URL, '/');
$pageTitle = 'Pengaturan';
include __DIR__ . '/../includes/header.php';
?>
<div style="display:flex; gap:60px; align-items:center;">
  <div>
    <div style="margin:12px 0; display:flex; align-items:center; gap:20px;">
      <div style="font-size:18px; min-width:180px;">Ubah Password</div>
      <a class="btn secondary" href="<?= $baseUrl ?>/pages/ubah_password.php">Ubah</a>
    </div>
    <div style="margin:12px 0; display:flex; align-items:center; gap:20px;">
      <div style="font-size:18px; min-width:180px;">Ubah PIN</div>
      <a class="btn secondary" href="<?= $baseUrl ?>/pages/ubah_pin.php">Ubah</a>
    </div>
  </div>
</div>
<?php include __DIR__ . '/../includes/footer.php'; ?>
