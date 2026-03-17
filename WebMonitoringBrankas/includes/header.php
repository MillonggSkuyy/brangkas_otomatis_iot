<?php
if (session_status() === PHP_SESSION_NONE) {
    session_start();
}
$pageTitle = $pageTitle ?? 'Brankas';
require_once __DIR__ . '/../config/app.php';
$baseUrl = rtrim(BASE_URL, '/');
?><!DOCTYPE html>
<html lang="id">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title><?= htmlspecialchars($pageTitle) ?> - MyBrangkas</title>
  <link rel="stylesheet" href="<?= $baseUrl ?>/assets/css/style.css">
</head>
<body>
<div class="layout">
  <aside class="sidebar">
    <div class="brand">MyBrangkas</div>

    <!-- AVATAR BARU -->
    <div class="profile-block" style="background:#212121; padding:16px 0; display:flex; justify-content:center; align-items:center;">
  <div style="width:88px; height:88px; border-radius:50%; overflow:hidden; background:#c8c8c8; flex:0 0 auto;">
    <img src="<?= $baseUrl ?>/assets/img/avatar.png" alt="Avatar"
         style="width:100%; height:100%; object-fit:cover; display:block;">
  </div>
</div>



    <div class="menu">
      <a href="<?= $baseUrl ?>/pages/data_akses.php" class="<?= strpos($_SERVER['REQUEST_URI'], 'data_akses') !== false ? 'active' : '' ?>">
        <span class="icon">📄</span> <span>Data Akses</span>
      </a>
      <a href="<?= $baseUrl ?>/pages/isi_brankas.php" class="<?= strpos($_SERVER['REQUEST_URI'], 'isi_brankas') !== false ? 'active' : '' ?>">
        <span class="icon">💼</span> <span>Isi Brankas</span>
      </a>
      <a href="<?= $baseUrl ?>/pages/settings.php" class="<?= strpos($_SERVER['REQUEST_URI'], 'setting') !== false || strpos($_SERVER['REQUEST_URI'], 'ubah_') !== false ? 'active' : '' ?>">
        <span class="icon">🛠️</span> <span>Pengaturan</span>
      </a>
    </div>
  </aside>

  <div class="content-wrap">
    <div class="topbar">
      <div class="title"><?= htmlspecialchars($pageTitle) ?></div>
      <a class="btn" style="background:#d72628; border-color:#8a1718;" href="<?= $baseUrl ?>/auth/logout.php">Keluar</a>
    </div>
    <div class="page">
