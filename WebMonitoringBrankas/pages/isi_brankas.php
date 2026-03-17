<?php
require __DIR__ . '/../includes/auth_guard.php';
require __DIR__ . '/../config/db.php';
require __DIR__ . '/../config/app.php';

$pageTitle = 'Isi Brankas';
$baseUrl = rtrim(BASE_URL, '/');
$errors = [];

// EDIT DIHAPUS: tetap dibiarkan ada variabelnya tapi tidak digunakan
$editId = 0;

$filterJenis = $_GET['filter'] ?? 'all'; // all | uang | emas | dokumen

function fmt_emas($x): string {
    return rtrim(rtrim(number_format((float)$x, 2, '.', ''), '0'), '.');
}
function fmt_rp($x): string {
    return 'Rp ' . number_format((int)$x, 0, ',', '.');
}

/**
 * Ambil saldo saat ini untuk validasi "keluar" + ringkasan
 */
function getSaldo(PDO $pdo): array {
    $stmt = $pdo->query("
        SELECT
          COALESCE(SUM(CASE
            WHEN jenis='uang' THEN
              (CASE WHEN aksi='masuk' THEN 1 ELSE -1 END) * (qty * COALESCE(uang_value,0))
            ELSE 0 END), 0) AS total_uang,

          COALESCE(SUM(CASE
            WHEN jenis='emas' THEN
              (CASE WHEN aksi='masuk' THEN 1 ELSE -1 END) * qty
            ELSE 0 END), 0) AS total_emas,

          COALESCE(SUM(CASE
            WHEN jenis='dokumen' THEN
              (CASE WHEN aksi='masuk' THEN 1 ELSE -1 END) * qty
            ELSE 0 END), 0) AS total_dokumen
        FROM brankas_items
    ");
    $row = $stmt->fetch(PDO::FETCH_ASSOC) ?: ['total_uang'=>0,'total_emas'=>0,'total_dokumen'=>0];

    return [
        'uang' => (int)$row['total_uang'],
        'emas' => (float)$row['total_emas'],
        'dokumen' => (int)$row['total_dokumen'],
    ];
}

/**
 * DELETE
 */
if (isset($_GET['delete_id'])) {
    $deleteId = (int)$_GET['delete_id'];
    if ($deleteId > 0) {
        $stmt = $pdo->prepare('DELETE FROM brankas_items WHERE id = ?');
        $stmt->execute([$deleteId]);

        $redir = $baseUrl . '/pages/isi_brankas.php?deleted=1';
        if (!empty($filterJenis) && $filterJenis !== 'all') $redir .= '&filter=' . urlencode($filterJenis);
        header('Location: ' . $redir);
        exit;
    }
}

/**
 * UPDATE (INLINE) - DIHAPUS
 * (Tidak ada aksi edit lagi)
 */

/**
 * INSERT (FORM ATAS - MASUK / KELUAR)
 */
if ($_SERVER['REQUEST_METHOD'] === 'POST' && !isset($_POST['action'])) {
    $tanggal = $_POST['tanggal'] ?? '';
    $jenis = $_POST['jenis'] ?? '';
    $aksi = $_POST['aksi'] ?? 'masuk';

    if (!preg_match('/^\d{4}-\d{2}-\d{2}$/', $tanggal)) $errors[] = 'Tanggal tidak valid.';
    if (!in_array($jenis, ['uang', 'emas', 'dokumen'], true)) $errors[] = 'Jenis isi wajib dipilih.';
    if (!in_array($aksi, ['masuk', 'keluar'], true)) $errors[] = 'Aksi tidak valid.';

    $item = '';
    $qty = 0;
    $unit = '';
    $jumlahText = '';
    $uangValue = null;

    if ($jenis === 'uang') {
        $uangJumlah = (int)($_POST['uang_jumlah'] ?? 0);
        $uangNominal = trim((string)($_POST['uang_value'] ?? ''));

        if ($uangJumlah <= 0) $errors[] = 'Jumlah uang harus diisi (minimal 1).';

        // Nominal uang sekarang hanya boleh 50.000 atau 100.000
        if ($uangNominal === '' || !ctype_digit($uangNominal)) {
            $errors[] = 'Nominal uang wajib dipilih.';
        } elseif (!in_array($uangNominal, ['50000', '100000'], true)) {
            $errors[] = 'Nominal uang tidak valid. Pilih Rp 50.000 atau Rp 100.000.';
        }

        if (empty($errors)) {
            $item = 'Uang';
            $qty = $uangJumlah;
            $unit = 'lembar';
            $uangValue = (int)$uangNominal;
            $jumlahText = $qty . ' lembar';
        }
    }

    if ($jenis === 'emas') {
        $emasJenis = trim($_POST['emas_jenis'] ?? '');
        $emasGram = trim($_POST['emas_gram'] ?? '');

        if ($emasJenis === '') $errors[] = 'Jenis emas wajib dipilih.';
        if ($emasGram === '' || !is_numeric($emasGram) || (float)$emasGram <= 0) $errors[] = 'Berat emas (gram) wajib angka > 0.';

        if (empty($errors)) {
            $item = 'Emas ' . $emasJenis;
            $qty = (float)$emasGram;
            $unit = 'gram';
            $uangValue = null;
            $jumlahText = fmt_emas($qty) . ' gram';
        }
    }

    if ($jenis === 'dokumen') {
        $dokJenis = trim($_POST['dokumen_jenis'] ?? '');
        $dokJumlah = (int)($_POST['dokumen_jumlah'] ?? 0);

        if ($dokJenis === '') $errors[] = 'Jenis dokumen wajib diisi (contoh: Sertifikat Tanah).';
        if ($dokJumlah <= 0) $errors[] = 'Jumlah dokumen harus diisi (minimal 1).';

        if (empty($errors)) {
            $item = 'Dokumen ' . $dokJenis;
            $qty = $dokJumlah;
            $unit = 'dokumen';
            $uangValue = null;
            $jumlahText = $qty . ' dokumen';
        }
    }

    // Validasi saldo kalau aksi=keluar
    if (empty($errors) && $aksi === 'keluar') {
        $saldo = getSaldo($pdo);

        if ($jenis === 'uang') {
            $ambil = (int)round($qty * (int)$uangValue);
            if ($saldo['uang'] < $ambil) $errors[] = 'Saldo uang tidak cukup untuk diambil.';
        } elseif ($jenis === 'emas') {
            if ($saldo['emas'] < $qty) $errors[] = 'Saldo emas tidak cukup untuk diambil.';
        } else {
            if ($saldo['dokumen'] < (int)round($qty)) $errors[] = 'Saldo dokumen tidak cukup untuk diambil.';
        }
    }

    if (empty($errors)) {
        $stmt = $pdo->prepare('
          INSERT INTO brankas_items (tanggal, jenis, aksi, item, qty, unit, jumlah_text, uang_value)
          VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        ');
        $stmt->execute([$tanggal, $jenis, $aksi, $item, $qty, $unit, $jumlahText, $uangValue]);

        $redir = $baseUrl . '/pages/isi_brankas.php?success=1';
        if (!empty($filterJenis) && $filterJenis !== 'all') $redir .= '&filter=' . urlencode($filterJenis);
        header('Location: ' . $redir);
        exit;
    }
}

/**
 * Ambil data sesuai filter rekap
 */
$sql = "SELECT id, tanggal, jenis, aksi, item, qty, unit, jumlah_text, uang_value, created_at
        FROM brankas_items";
$params = [];

if ($filterJenis !== 'all') {
    $sql .= " WHERE jenis = ?";
    $params[] = $filterJenis;
}

$sql .= " ORDER BY tanggal ASC, id ASC";
$stmt = $pdo->prepare($sql);
$stmt->execute($params);
$rows = $stmt->fetchAll(PDO::FETCH_ASSOC);

// Total ringkasan (saldo)
$saldo = getSaldo($pdo);

$keteranganList = [];
include __DIR__ . '/../includes/header.php';
?>

<div class="card" style="margin-bottom:20px;">
  <h3 style="margin-top:0;">Tambah Catatan Isi Brankas</h3>

  <?php if (!empty($errors)): ?>
    <div class="alert error">
      <?php foreach ($errors as $e): ?>
        <div><?= htmlspecialchars($e) ?></div>
      <?php endforeach; ?>
    </div>
  <?php elseif (isset($_GET['deleted'])): ?>
    <div class="alert success">Data berhasil dihapus.</div>
  <?php elseif (isset($_GET['updated'])): ?>
    <div class="alert success">Data berhasil diperbarui.</div>
  <?php elseif (isset($_GET['success'])): ?>
    <div class="alert success">Data berhasil disimpan.</div>
  <?php endif; ?>

  <form method="post" id="formTambah">
    <label for="tanggal">Tanggal dimasukkan</label>
    <input id="tanggal" name="tanggal" type="date"
           value="<?= htmlspecialchars($_POST['tanggal'] ?? date('Y-m-d')) ?>" required>

    <label for="jenis">Apa yang dimasukkan</label>
    <select id="jenis" name="jenis" required onchange="updateForm()">
      <option value="">-- Pilih Jenis --</option>
      <option value="uang" <?= (($_POST['jenis'] ?? '') === 'uang') ? 'selected' : '' ?>>Uang</option>
      <option value="emas" <?= (($_POST['jenis'] ?? '') === 'emas') ? 'selected' : '' ?>>Emas</option>
      <option value="dokumen" <?= (($_POST['jenis'] ?? '') === 'dokumen') ? 'selected' : '' ?>>Dokumen</option>
    </select>

    <div id="form-uang" style="display:none; margin-top:10px;">
      <label>Berapa (lembar/unit)</label>
      <input type="number" name="uang_jumlah" min="1"
             value="<?= htmlspecialchars($_POST['uang_jumlah'] ?? '') ?>">

      <label>Nominal per unit (Rp)</label>
      <select name="uang_value">
        <option value="">-- Pilih Nominal --</option>
        <option value="50000" <?= (($_POST['uang_value'] ?? '') === '50000') ? 'selected' : '' ?>>Rp 50.000</option>
        <option value="100000" <?= (($_POST['uang_value'] ?? '') === '100000') ? 'selected' : '' ?>>Rp 100.000</option>
      </select>
    </div>

    <div id="form-emas" style="display:none; margin-top:10px;">
      <label>Jenis emas</label>
      <select name="emas_jenis">
        <option value="">-- Pilih Jenis Emas --</option>
        <option value="Batangan/Logam Mulia" <?= (($_POST['emas_jenis'] ?? '') === 'Batangan/Logam Mulia') ? 'selected' : '' ?>>Batangan/Logam Mulia</option>
        <option value="Kalung" <?= (($_POST['emas_jenis'] ?? '') === 'Kalung') ? 'selected' : '' ?>>Kalung</option>
        <option value="Cincin" <?= (($_POST['emas_jenis'] ?? '') === 'Cincin') ? 'selected' : '' ?>>Cincin</option>
        <option value="Gelang" <?= (($_POST['emas_jenis'] ?? '') === 'Gelang') ? 'selected' : '' ?>>Gelang</option>
        <option value="Liontin" <?= (($_POST['emas_jenis'] ?? '') === 'Liontin') ? 'selected' : '' ?>>Liontin</option>
        <option value="Anting" <?= (($_POST['emas_jenis'] ?? '') === 'Anting') ? 'selected' : '' ?>>Anting</option>
        <option value="Giwang" <?= (($_POST['emas_jenis'] ?? '') === 'Giwang') ? 'selected' : '' ?>>Giwang</option>
        <option value="Bros" <?= (($_POST['emas_jenis'] ?? '') === 'Bros') ? 'selected' : '' ?>>Bros</option>
        <option value="Gigi Emas" <?= (($_POST['emas_jenis'] ?? '') === 'Gigi Emas') ? 'selected' : '' ?>>Gigi Emas</option>
        <option value="Koin Emas" <?= (($_POST['emas_jenis'] ?? '') === 'Koin Emas') ? 'selected' : '' ?>>Koin Emas</option>
        <option value="Lainnya" <?= (($_POST['emas_jenis'] ?? '') === 'Lainnya') ? 'selected' : '' ?>>Lainnya</option>
      </select>

      <label>Berapa gram</label>
      <input type="number" name="emas_gram" min="0" step="0.01"
             value="<?= htmlspecialchars($_POST['emas_gram'] ?? '') ?>"
             placeholder="">
    </div>

    <div id="form-dokumen" style="display:none; margin-top:10px;">
      <label>Jenis dokumen</label>
      <input type="text" name="dokumen_jenis"
             value="<?= htmlspecialchars($_POST['dokumen_jenis'] ?? '') ?>"
             placeholder="">

      <label>Berapa dokumen</label>
      <input type="number" name="dokumen_jumlah" min="1"
             value="<?= htmlspecialchars($_POST['dokumen_jumlah'] ?? '') ?>"
             placeholder="">
    </div>

    <div style="display:flex; gap:10px; margin-top:10px;">
      <button type="submit" name="aksi" value="masuk">Simpan</button>
      <button type="submit" name="aksi" value="keluar" style="background:#444;">Ambil</button>
    </div>
  </form>
</div>

<div class="card">
  <h3 style="margin-top:0;">Rekap Isi Brankas</h3>

  <form method="get" style="margin-bottom:10px;">
    <label for="filter"><b>Filter Rekap:</b></label>
    <select id="filter" name="filter" onchange="this.form.submit()">
      <option value="all" <?= $filterJenis==='all'?'selected':'' ?>>Semua</option>
      <option value="uang" <?= $filterJenis==='uang'?'selected':'' ?>>Uang</option>
      <option value="emas" <?= $filterJenis==='emas'?'selected':'' ?>>Emas</option>
      <option value="dokumen" <?= $filterJenis==='dokumen'?'selected':'' ?>>Dokumen</option>
    </select>
  </form>

  <table>
    <thead>
      <tr>
        <th>NO</th>
        <th>TANGGAL</th>
        <th>AKSI</th>
        <th>ISI</th>
        <th>QTY</th>
        <th>UNIT</th>
        <th>NOMINAL (Rp)</th>
        <th>TOTAL (Rp)</th>
        <th>AKSI</th>
      </tr>
    </thead>

    <tbody>
      <?php if (empty($rows)): ?>
        <tr>
          <td colspan="9" style="text-align:center;">Belum ada data</td>
        </tr>
      <?php else: ?>

        <?php
        $runningUang = 0; // saldo uang berjalan untuk tampilan
        foreach ($rows as $i => $r):
          $isUang = ($r['jenis'] === 'uang');
          $sign = ($r['aksi'] === 'masuk') ? 1 : -1;

          if ($isUang) {
            $unitMoney = (int)($r['uang_value'] ?? 0);
            $lineTotal = $sign * ((float)$r['qty'] * $unitMoney);
            $runningUang += (int)round($lineTotal);
          }

          $keteranganList[] = $r['tanggal'] . ' - ' . strtoupper($r['aksi']) . ' - ' . $r['item'] . ' (' . $r['jumlah_text'] . ')';
        ?>

            <tr>
              <td><?= $i + 1 ?></td>
              <td><?= htmlspecialchars($r['tanggal']) ?></td>

              <td>
                <b style="color:<?= ($r['aksi']==='masuk'?'#0a7':'#c00') ?>;">
                  <?= htmlspecialchars(strtoupper($r['aksi'])) ?>
                </b>
              </td>

              <td><?= htmlspecialchars($r['item']) ?></td>

              <td>
                <?= htmlspecialchars($r['jenis']==='emas' ? fmt_emas($r['qty']) : (string)((int)round($r['qty']))) ?>
              </td>

              <td><?= htmlspecialchars($r['unit']) ?></td>

              <td>
                <?= $isUang && $r['uang_value'] !== null ? fmt_rp((int)$r['uang_value']) : '-' ?>
              </td>

              <td>
                <?= $isUang ? fmt_rp($runningUang) : '-' ?>
              </td>

              <td style="white-space:nowrap;">
                <a href="<?= $baseUrl . '/pages/isi_brankas.php?delete_id=' . (int)$r['id'] . ($filterJenis !== 'all' ? '&filter=' . urlencode($filterJenis) : '') ?>"
                   onclick="return confirm('Yakin ingin menghapus data ini?');">Hapus</a>
              </td>
            </tr>

        <?php endforeach; ?>
      <?php endif; ?>
    </tbody>
  </table>
</div>

<div class="card" style="margin-top:20px;">
  <h4 style="margin-top:0;">Ringkasan Saldo</h4>
  <div><b>Total uang:</b> <?= fmt_rp($saldo['uang']) ?></div>
  <div><b>Total emas:</b> <?= fmt_emas($saldo['emas']) ?> gram</div>
  <div><b>Total dokumen:</b> <?= (int)$saldo['dokumen'] ?> dokumen</div>
</div>

<div class="card" style="margin-top:20px;">
  <h4 style="margin-top:0;">Keterangan (ringkasan semua transaksi)</h4>
  <?php if (empty($keteranganList)): ?>
    <div>Belum ada catatan.</div>
  <?php else: ?>
    <ul style="padding-left:18px; margin:0;">
      <?php foreach ($keteranganList as $itemText): ?>
        <li><?= htmlspecialchars($itemText) ?></li>
      <?php endforeach; ?>
    </ul>
  <?php endif; ?>
</div>

<script>
function updateForm() {
  const jenis = document.getElementById('jenis').value;
  document.getElementById('form-uang').style.display = 'none';
  document.getElementById('form-emas').style.display = 'none';
  document.getElementById('form-dokumen').style.display = 'none';
  if (jenis === 'uang') document.getElementById('form-uang').style.display = 'block';
  if (jenis === 'emas') document.getElementById('form-emas').style.display = 'block';
  if (jenis === 'dokumen') document.getElementById('form-dokumen').style.display = 'block';
}
document.addEventListener('DOMContentLoaded', updateForm);
</script>

<?php include __DIR__ . '/../includes/footer.php'; ?>