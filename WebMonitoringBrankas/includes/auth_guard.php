<?php
session_start();
require_once __DIR__ . '/../config/app.php';
$baseUrl = rtrim(BASE_URL, '/');
if (empty($_SESSION['user'])) {
    header('Location: ' . $baseUrl . '/auth/login.php');
    exit;
}
