<?php
session_start();
require_once __DIR__ . '/../config/db.php';
require_once __DIR__ . '/../config/app.php';
$baseUrl = rtrim(BASE_URL, '/');

if (!empty($_SESSION['user'])) {
    header('Location: ' . $baseUrl . '/pages/data_akses.php');
    exit;
}

$error = '';
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $username = trim($_POST['username'] ?? '');
    $password = $_POST['password'] ?? '';

    $stmt = $pdo->prepare('SELECT id, username, password_hash, role FROM users WHERE username = ?');
    $stmt->execute([$username]);
    $user = $stmt->fetch();

    if ($user && password_verify($password, $user['password_hash'])) {
        $_SESSION['user'] = [
            'id' => $user['id'],
            'username' => $user['username'],
            'role' => $user['role'],
        ];
        header('Location: ' . $baseUrl . '/pages/data_akses.php');
        exit;
    }
    $error = 'Username atau password salah.';
}
?>
<!DOCTYPE html>
<html lang="id">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Login MyBrangkas</title>
  <?php $baseUrl = rtrim(BASE_URL, '/'); ?>
  <link rel="stylesheet" href="<?= $baseUrl ?>/assets/css/style.css">
</head>
<body class="login-page">
  <div class="login-top">Login MyBrangkas</div>
  <div class="login-body">
    <div class="login-card">
      <?php if ($error): ?><div class="alert error"><?= htmlspecialchars($error) ?></div><?php endif; ?>
      <form method="post">
        <label>Username</label>
        <input name="username" placeholder="Username" required>
        <label>Password</label>
        <input type="password" name="password" placeholder="Password" required>
        <button type="submit">Masuk</button>
      </form>
    </div>
  </div>
</body>
</html>
