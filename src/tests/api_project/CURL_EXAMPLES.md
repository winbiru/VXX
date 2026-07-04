# api_project - Run and Call via Postman/curl

## Start project (V++ style)

```bash
cd /Users/winbiru/V++/src/tests/api_project
./start_api_project.sh
```

Default port: `8080`.

## Health

```bash
curl -s http://localhost:8080/actuator/health
```

## Login to get token payload format

```bash
curl -s -X POST http://localhost:8080/api/v1/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"username":"admin","password":"123456"}'
```

## Get all users

```bash
curl -s 'http://localhost:8080/api/v1/admin/users?requestId=req-001' \
  -H 'Authorization: Bearer demo-token'
```

## Search users

```bash
curl -s 'http://localhost:8080/api/v1/admin/users/search?keyword=an&requestId=req-002' \
  -H 'Authorization: Bearer demo-token'
```

## Create user

```bash
curl -s -X POST 'http://localhost:8080/api/v1/admin/users?requestId=req-003' \
  -H 'Authorization: Bearer demo-token' \
  -H 'Content-Type: application/json' \
  -d '{"username":"khanh","first_names":"Khanh","last_name":"Le","email":"khanh@example.com"}'
```

## Update user (replace `{id}` with real id)

```bash
curl -s -X PUT 'http://localhost:8080/api/v1/admin/users/1?requestId=req-004' \
  -H 'Authorization: Bearer demo-token' \
  -H 'Content-Type: application/json' \
  -d '{"first_names":"An Updated","email":"an.updated@example.com"}'
```

## Delete user

```bash
curl -s -X DELETE 'http://localhost:8080/api/v1/admin/users/1?requestId=req-005' \
  -H 'Authorization: Bearer demo-token'
```
