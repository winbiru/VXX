import json, os, pathlib, subprocess, tempfile, time, urllib.request, urllib.error, shutil, socket
root=pathlib.Path(__file__).resolve().parents[3]
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

def request(path, body=None, expected=200):
    global count
    data=None if body is None else json.dumps(body,ensure_ascii=False).encode()
    req=urllib.request.Request('http://127.0.0.1:8088'+path,data=data,headers={'Content-Type':'application/json'})
    try: response=client.open(req,timeout=10)
    except urllib.error.HTTPError as error: response=error
    with response:
        status=response.status
        raw=response.read().decode()
    assert status==expected,(path,status,raw)
    count+=1
    return json.loads(raw)

def start():
    global p,log
    log=open(work/'server.log','a')
    p=subprocess.Popen([str(root/'build-sanitize-local/bin/vpp-cli'),'chạy',str(app/'src/main.vi')],cwd=work,env=env,stdout=log,stderr=log)
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
    start()
    assert request('/san-pham')['duLieu']==[]
    product=request('/san-pham',{'id':'SP-1','ten':'Cà phê "Đậm" ☕','gia':180000,'tonKho':10},201)['duLieu']
    assert product['ten']=='Cà phê "Đậm" ☕'
    assert request('/san-pham/nhap-kho',{'id':'SP-1','soLuong':5})['duLieu']['tonKho']==15
    order=request('/don-hang',{'khachHang':'Minh Anh','sanPham':[{'maSanPham':'SP-1','soLuong':2},{'maSanPham':'SP-1','soLuong':1}]},201)['duLieu']
    assert order['tongTien']==540000 and len(order['cacDong'])==1
    assert request('/san-pham')['duLieu'][0]['tonKho']==12
    assert request('/bao-cao')['duLieu']['doanhThu']==540000
    assert request('/san-pham/thong-ke')['duLieu']['tongDaBan']==3
    assert request('/don-hang',{'khachHang':'Lỗi','sanPham':[{'maSanPham':'SP-1','soLuong':9999}]},400)['thanhCong']==0
    assert request('/san-pham')['duLieu'][0]['tonKho']==12
    assert request('/don-hang/huy',{'id':order['id']})['duLieu']['trangThai']=='DA_HUY'
    request('/don-hang/huy',{'id':order['id']},400)
    assert request('/san-pham')['duLieu'][0]['tonKho']==15
    request('/khong-ton-tai',expected=404)
    print('PASS: HTTP routes, Unicode JSON, stock/order/cancellation, 400/404',flush=True)
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
