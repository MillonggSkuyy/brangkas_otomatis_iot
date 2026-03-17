<?php
session_start();
require __DIR__ . '/../config/app.php';
$baseUrl = rtrim(BASE_URL, '/');
session_destroy();
header('Location: ' . $baseUrl . '/auth/login.php');
exit;
