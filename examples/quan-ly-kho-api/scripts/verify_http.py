import concurrent.futures, json, os, pathlib, subprocess, tempfile, time, urllib.request, urllib.error, shutil, socket
root=pathlib.Path(__file__).resolve().parents[3]

def find_vpp():
    configured=os.environ.get('VPP_EXEC')
    if configured:
        return configured

    candidates=[
        root/'build-sanitize-local/bin/vpp-cli',
        root/'build/bin/vpp-cli',
        root/'vpp',
    ]
    for candidate in candidates:
        if candidate.is_file():
            return str(candidate)

    installed=shutil.which('vpp')
    if installed:
        return installed

    raise SystemExit('Không tìm thấy V++ executable. Cài V++0.9 release hoặc đặt VPP_EXEC.')

vpp=find_vpp()
with socket.socket() as probe:
    if probe.connect_ex(('127.0.0.1', 8088)) == 0:
        raise SystemExit('Port 8088 is in use; stop the existing server before verification.')
work=pathlib.Path(tempfile.mkdtemp(prefix='vpp-http-'))
app=work/'app'
shutil.copytree(root/'examples/quan-ly-kho-api', app, ignore=shutil.ignore_patterns('data', '.vpp'))
env=dict(os.environ, VPP_HOME=str(root))
client=urllib.request.build_opener(urllib.request.ProxyHandler({}))
count=0
p=None
log=None

def request(path, body=None, expected=200, method=None):
    global count
    data=None if body is None else json.dumps(body,ensure_ascii=False).encode()
    req=urllib.request.Request('http://127.0.0.1:8088'+path,data=data,headers={'Content-Type':'application/json'},method=method)
    try: response=client.open(req,timeout=10)
    except urllib.error.HTTPError as error: response=error
    with response:
        status=response.status
        raw=response.read().decode()
    assert status==expected,(path,status,raw)
    count+=1
    return json.loads(raw)

def health_request(_):
    return request('/health')['trangThai']

def start():
    global p,log
    log=open(work/'server.log','a')
    p=subprocess.Popen([vpp,'chạy',str(app/'src/main.vi')],cwd=work,env=env,stdout=log,stderr=log)
    deadline=time.monotonic()+25
    while time.monotonic()<deadline:
        if p.poll() is not None: raise RuntimeError((work/'server.log').read_text())
        try:
            assert request('/health')['trangThai']=='UP'
            return
        except urllib.error.URLError: time.sleep(.15)
    raise RuntimeError('server startup timed out')

def stop():
    global p,log
    if p is not None:
        p.terminate()
        try:p.wait(timeout=5)
        except subprocess.TimeoutExpired:p.kill();p.wait()
        p=None
    if log:log.close();log=None

try:
    transaction=subprocess.run(
        [vpp,'chạy',str(app/'src/transaction_check.vi')],
        cwd=work,
        env=env,
        capture_output=True,
        text=True,
    )
    assert transaction.returncode==0,(transaction.stdout,transaction.stderr)
    print(transaction.stdout.strip(),flush=True)
    start()
    features=request('/features')['duLieu']
    assert features and all(value==1 for value in features.values())
    assert request('/san-pham')['duLieu']==[]
    product=request('/san-pham',{'id':'SP-1','ten':'Cà phê "Đậm" ☕','gia':180000,'tonKho':10},201)['duLieu']
    assert product['ten']=='Cà phê "Đậm" ☕'
    assert request('/san-pham/nhap-kho',{'id':'SP-1','soLuong':5})['duLieu']['tonKho']==15
    crud=request('/san-pham',{'id':'SP-CRUD','ten':'Tra kiem thu','gia':50000,'tonKho':4},201)['duLieu']
    assert crud['id']=='SP-CRUD'
    assert request('/san-pham/chi-tiet',{'id':'SP-CRUD'})['duLieu']['ten']=='Tra kiem thu'
    search=request('/san-pham/tim-kiem',{'tuKhoa':'kiem','trang':1,'kichThuoc':1})['duLieu']
    assert search['tong']==1 and len(search['duLieu'])==1 and search['duLieu'][0]['id']=='SP-CRUD'
    updated=request('/san-pham',{'id':'SP-CRUD','ten':'Tra kiem thu moi','gia':55000,'tonKho':7},method='PUT')['duLieu']
    assert updated['ten']=='Tra kiem thu moi' and updated['gia']==55000 and updated['tonKho']==7
    removed=request('/san-pham',{'id':'SP-CRUD'},method='DELETE')['duLieu']
    assert removed['id']=='SP-CRUD'
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        health=list(pool.map(health_request, range(16)))
    assert health==['UP']*16
    order=request('/don-hang',{'khachHang':'Minh Anh','sanPham':[{'maSanPham':'SP-1','soLuong':2},{'maSanPham':'SP-1','soLuong':1}]},201)['duLieu']
    assert order['tongTien']==540000 and len(order['cacDong'])==1
    detail=request('/don-hang/chi-tiet',{'id':order['id']})['duLieu']
    assert detail['id']==order['id'] and detail['tongTien']==540000
    assert request('/san-pham')['duLieu'][0]['tonKho']==12
    assert request('/bao-cao')['duLieu']['doanhThu']==540000
    assert request('/san-pham/thong-ke')['duLieu']['tongDaBan']==3
    assert request('/don-hang',{'khachHang':'Lỗi','sanPham':[{'maSanPham':'SP-1','soLuong':9999}]},400)['thanhCong']==0
    assert request('/san-pham')['duLieu'][0]['tonKho']==12
    assert request('/don-hang/huy',{'id':order['id']})['duLieu']['trangThai']=='DA_HUY'
    request('/don-hang/huy',{'id':order['id']},400)
    assert request('/san-pham')['duLieu'][0]['tonKho']==15
    request('/khong-ton-tai',expected=404)
    print('PASS: feature gate, CRUD/search, concurrent health, Unicode JSON, stock/order/cancellation, 400/404',flush=True)
    for i in range(25):
        created=request('/don-hang',{'khachHang':'Lặp '+str(i),'sanPham':[{'maSanPham':'SP-1','soLuong':1}]},201)['duLieu']
        request('/don-hang/huy',{'id':created['id']})
        assert request('/san-pham')['duLieu'][0]['tonKho']==15
    before=request('/bao-cao')['duLieu']
    assert before['soDonHang']==26 and before['doanhThu']==0
    stop()
    start()
    assert request('/bao-cao')['duLieu']==before
    assert len(request('/don-hang')['duLieu'])==26
    assert request('/san-pham')['duLieu'][0]['tonKho']==15
    files={f.name:json.loads(f.read_text()) for f in (app/'src/data').glob('*.json')}
    assert len(files['don-hang.json'])==26
    print('PASS: 25 repeated order/cancel cycles; persisted state survives restart',flush=True)
    print(json.dumps({'requests_checked':count,'report':before,'artifacts':str(work)},ensure_ascii=False),flush=True)
finally:
    stop()
    print((work/'server.log').read_text()[-2500:])
