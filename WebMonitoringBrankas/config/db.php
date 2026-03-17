<?php
$DB_HOST = 'localhost';
$DB_NAME = 'u597126378_brankas';
$DB_USER = 'u597126378_kamil';
$DB_PASS = '@Kamil123123';

function ensureConnection(string $host, string $db, string $user, string $pass): PDO {
    try {
        return new PDO("mysql:host={$host};dbname={$db};charset=utf8mb4", $user, $pass, [
            PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
            PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
        ]);
    } catch (PDOException $e) {
        if ((int)$e->getCode() !== 1049) { // Unknown database
            throw $e;
        }
        $rootPdo = new PDO("mysql:host={$host};charset=utf8mb4", $user, $pass, [
            PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
        ]);
        $rootPdo->exec("CREATE DATABASE IF NOT EXISTS `{$db}` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci");
        return new PDO("mysql:host={$host};dbname={$db};charset=utf8mb4", $user, $pass, [
            PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
            PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
        ]);
    }
}

function ensureSchema(PDO $pdo): void {
    $pdo->exec("CREATE TABLE IF NOT EXISTS users (
        id INT AUTO_INCREMENT PRIMARY KEY,
        username VARCHAR(50) NOT NULL UNIQUE,
        password_hash VARCHAR(255) NOT NULL,
        role ENUM('admin','viewer') NOT NULL DEFAULT 'admin',
        created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
    ) ENGINE=InnoDB CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci");

    $pdo->exec("CREATE TABLE IF NOT EXISTS akses (
        id BIGINT AUTO_INCREMENT PRIMARY KEY,
        tanggal DATE NOT NULL,
        jam TIME NOT NULL,
        aktivitas VARCHAR(255) NOT NULL,
        foto VARCHAR(255) NOT NULL,
        video VARCHAR(255) DEFAULT NULL,
        created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
    ) ENGINE=InnoDB CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci");

    $pdo->exec("CREATE TABLE IF NOT EXISTS settings (
        id TINYINT PRIMARY KEY DEFAULT 1,
        pin_brangkas VARCHAR(20) NOT NULL,
        telegram_bot_token VARCHAR(120) DEFAULT NULL,
        telegram_chat_id VARCHAR(40) DEFAULT NULL,
        updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
    ) ENGINE=InnoDB CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci");

    $pdo->exec("CREATE TABLE IF NOT EXISTS brankas_items (
        id INT AUTO_INCREMENT PRIMARY KEY,
        tanggal DATE NOT NULL,
        item VARCHAR(100) NOT NULL,
        jumlah_text VARCHAR(100) NOT NULL,
        uang_value BIGINT DEFAULT NULL,
        created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
    ) ENGINE=InnoDB CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci");

    // ✅ FIX: jangan timpa pin_brangkas kalau sudah ada
    $pdo->exec("INSERT IGNORE INTO settings (id, pin_brangkas) VALUES (1, '1234')");

    $hasUser = $pdo->query('SELECT COUNT(*) FROM users')->fetchColumn();
    if ((int)$hasUser === 0) {
        $hash = password_hash('admin123', PASSWORD_DEFAULT);
        $stmt = $pdo->prepare('INSERT INTO users (username, password_hash, role) VALUES (?, ?, ?)');
        $stmt->execute(['admin', $hash, 'admin']);
    }
}

try {
    $pdo = ensureConnection($DB_HOST, $DB_NAME, $DB_USER, $DB_PASS);
    ensureSchema($pdo);
} catch (PDOException $e) {
    http_response_code(500);
    exit('Database connection failed');
}
