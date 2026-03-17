<?php
require __DIR__ . '/../includes/auth_guard.php';
require __DIR__ . '/../config/db.php';
require __DIR__ . '/../config/app.php';

$pageTitle = 'Data Akses';
$baseUrl = rtrim(BASE_URL, '/');
$errors = [];

/**
 * === FIX TIMEZONE (WITA / Asia/Makassar) ===
 * Pastikan semua fungsi tanggal PHP pakai Asia/Makassar
 */
date_default_timezone_set('Asia/Makassar');

/**
 * (Opsional tapi disarankan) set timezone session MySQL.
 * Ini berpengaruh kalau kamu pakai NOW(), CURRENT_TIMESTAMP, created_at default, dll.
 * Kalau hosting tidak punya timezone tables, gunakan +08:00.
 */
try {
    $pdo->exec("SET time_zone = 'Asia/Makassar'");
} catch (Throwable $e) {
    // fallback kalau 'Asia/Makassar' tidak dikenali di server MySQL hosting
    try { $pdo->exec("SET time_zone = '+08:00'"); } catch (Throwable $e2) {}
}

/**
 * DELETE (aksi hapus) - tetap di halaman ini
 */
if (isset($_GET['delete_id'])) {
    $deleteId = (int)$_GET['delete_id'];
    if ($deleteId > 0) {
        // Ambil nama file dulu (biar sekalian bisa hapus file di folder)
        $stmt = $pdo->prepare('SELECT foto, video FROM akses WHERE id = ?');
        $stmt->execute([$deleteId]);
        $data = $stmt->fetch();

        // Hapus data dari DB
        $stmt = $pdo->prepare('DELETE FROM akses WHERE id = ?');
        $stmt->execute([$deleteId]);

        // Optional: hapus file fisik kalau ada
        if ($data) {
            if (!empty($data['foto'])) {
                $fotoPath = __DIR__ . '/../uploads/foto/' . $data['foto'];
                if (is_file($fotoPath)) @unlink($fotoPath);
            }
            if (!empty($data['video'])) {
                $videoPath = __DIR__ . '/../uploads/video/' . $data['video'];
                if (is_file($videoPath)) @unlink($videoPath);
            }
        }

        header('Location: ' . $baseUrl . '/pages/data_akses.php?deleted=1');
        exit;
    }
}

/**
 * Ambil data
 * NOTE:
 * - Kalau jam/tanggal disimpan sebagai string biasa, timezone server tidak akan mengubahnya.
 * - Kalau kamu punya created_at (timestamp/datetime) yang benar, bisa dipakai untuk jam tampil.
 */
$rows = $pdo->query('SELECT id, tanggal, jam, aktivitas, foto, video, created_at FROM akses ORDER BY created_at DESC')->fetchAll();

include __DIR__ . '/../includes/header.php';
?>
<div class="card">

  <?php if (isset($_GET['deleted'])): ?>
    <div class="alert success">Data berhasil dihapus.</div>
  <?php endif; ?>

  <table>
    <thead>
      <tr>
        <th>NO</th>
        <th>TANGGAL</th>
        <th>JAM</th>
        <th>AKTIVITAS</th>
        <th>FOTO</th>
        <th>VIDEO</th>
        <th>AKSI</th>
      </tr>
    </thead>
    <tbody>
      <?php if (empty($rows)): ?>
        <tr><td colspan="7" style="text-align:center;">Belum ada data</td></tr>
      <?php else: ?>
        <?php foreach ($rows as $i => $r): ?>

          <?php
            /**
             * === TAMPIL JAM AGAR SESUAI ASIA/MAKASSAR ===
             * Prioritas:
             * 1) Jika created_at ada, pakai created_at sebagai sumber jam (paling konsisten di server)
             * 2) Jika created_at kosong, fallback ke kolom jam yang ada di DB.
             *
             * Kalau created_at kamu disimpan UTC dan MySQL timezone belum benar,
             * kamu bisa konversi manual di PHP (lihat blok di bawah).
             */
            $tanggalTampil = $r['tanggal'];
            $jamTampil = $r['jam'];

            // Jika created_at terisi, gunakan untuk tanggal & jam tampil (lebih akurat untuk timezone)
            if (!empty($r['created_at'])) {
                try {
                    // Anggap created_at sudah "local time" server setelah SET time_zone.
                    // Kalau created_at ternyata UTC, lihat catatan konversi manual di bawah.
                    $dt = new DateTime($r['created_at']);
                    $dt->setTimezone(new DateTimeZone('Asia/Makassar'));

                    // Jika kamu mau tanggal dari created_at:
                    // $tanggalTampil = $dt->format('Y-m-d');

                    // Jam dari created_at:
                    $jamTampil = $dt->format('H:i:s');
                } catch (Throwable $e) {
                    // fallback tetap pakai yang dari DB
                }
            }

            /**
             * Jika created_at kamu sebenarnya UTC (umum di hosting) dan SET time_zone tidak ngaruh,
             * kamu bisa pakai konversi ini (uncomment):
             *
             * if (!empty($r['created_at'])) {
             *     $dt = new DateTime($r['created_at'], new DateTimeZone('UTC'));
             *     $dt->setTimezone(new DateTimeZone('Asia/Makassar'));
             *     $jamTampil = $dt->format('H:i:s');
             *     // $tanggalTampil = $dt->format('Y-m-d');
             * }
             */
          ?>

          <tr>
            <td><?= $i + 1 ?></td>
            <td><?= htmlspecialchars($tanggalTampil) ?></td>
            <td><?= htmlspecialchars($jamTampil) ?></td>
            <td><?= htmlspecialchars($r['aktivitas']) ?></td>

            <!-- FOTO -->
            <td>
              <?php if (!empty($r['foto'])): ?>
                <a class="btn secondary"
                   href="<?= $baseUrl ?>/uploads/foto/<?= urlencode($r['foto']) ?>"
                   target="_blank">Lihat</a>
              <?php else: ?>
                -
              <?php endif; ?>
            </td>

            <!-- VIDEO -->
            <td>
              <?php if (!empty($r['video'])): ?>
                <a class="btn secondary"
                   href="<?= $baseUrl ?>/uploads/video/<?= urlencode($r['video']) ?>"
                   target="_blank">Lihat</a>
              <?php else: ?>
                -
              <?php endif; ?>
            </td>

            <!-- AKSI HAPUS -->
            <td>
              <a class="btn"
                 href="<?= $baseUrl ?>/pages/data_akses.php?delete_id=<?= (int)$r['id'] ?>"
                 onclick="return confirm('Yakin ingin menghapus data ini?');">Hapus</a>
            </td>
          </tr>
        <?php endforeach; ?>
      <?php endif; ?>
    </tbody>
  </table>
</div>
<?php include __DIR__ . '/../includes/footer.php'; ?>