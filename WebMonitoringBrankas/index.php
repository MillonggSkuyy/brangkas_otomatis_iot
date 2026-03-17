<?php
require_once __DIR__ . '/config/app.php';
$baseUrl = rtrim(BASE_URL, '/');
header('Location: ' . $baseUrl . '/auth/login.php');
exit;
