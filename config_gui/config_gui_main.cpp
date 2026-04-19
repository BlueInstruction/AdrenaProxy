// AdrenaProxy Config GUI - standalone ImGui + DX11 desktop editor
// for adrena_proxy.ini. No game injection needed.
// Build: cmake --build build --target adrena_config
#include <windows.h>
#include <d3d11.h>
#include <string>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

struct IniCfg {
    bool en=false; int mode=1,qual=1; float cscale=0,sharp=0.8f;
    char up[32]="sgsr1"; int fg=0,fpst=0; bool ov=true,fpsd=true;
    float opa=0.85f; bool fd11=false; int rtf=0,mfq=2,vs=0;
    float fsr2r=0; int xp=0; std::string path;
};
static std::string Trim(const std::string&s){auto a=s.find_first_not_of(" \t\r\n");if(a==std::string::npos)return"";return s.substr(a,s.find_last_not_of(" \t\r\n")-a+1);}
static void Load(IniCfg&c){std::ifstream f(c.path);if(!f)return;std::string sec,ln;while(std::getline(f,ln)){ln=Trim(ln);if(ln.empty()||ln[0]==';')continue;if(ln[0]=='['){auto e=ln.find(']');if(e!=std::string::npos)sec=ln.substr(1,e-1);continue;}auto eq=ln.find('=');if(eq==std::string::npos)continue;auto k=Trim(ln.substr(0,eq)),v=Trim(ln.substr(eq+1));try{if(sec=="SGSR"){if(k=="enabled")c.en=std::stoi(v)!=0;else if(k=="mode")c.mode=(v=="sgsr2")?2:(v=="sgsr1")?1:0;else if(k=="custom_scale")c.cscale=std::stof(v);else if(k=="sharpness")c.sharp=std::stof(v);else if(k=="quality"){const char*q[]={"ultra_quality","quality","balanced","performance","ultra_performance"};for(int i=0;i<5;i++)if(v==q[i])c.qual=i;}}else if(sec=="upscaler"&&k=="mode")strncpy(c.up,v.c_str(),31);else if(sec=="FrameGeneration"){if(k=="mode")c.fg=(v=="x2")?1:(v=="x3")?2:(v=="x4")?3:0;else if(k=="fps_threshold")c.fpst=std::stoi(v);}else if(sec=="Overlay"){if(k=="enabled")c.ov=std::stoi(v)!=0;else if(k=="fps_display")c.fpsd=std::stoi(v)!=0;else if(k=="opacity")c.opa=std::stof(v);}else if(sec=="Advanced"){if(k=="force_d3d11")c.fd11=std::stoi(v)!=0;else if(k=="max_frame_queue")c.mfq=std::stoi(v);else if(k=="vsync")c.vs=std::stoi(v);}else if(sec=="fsr2"&&k=="quality_ratio")c.fsr2r=std::stof(v);else if(sec=="xess"&&k=="quality_preset")c.xp=std::stoi(v);}catch(...){}}}
static void Save(const IniCfg&c){std::ofstream f(c.path);if(!f)return;const char*m[]={"off","sgsr1","sgsr2"},*q[]={"ultra_quality","quality","balanced","performance","ultra_performance"};f<<"; AdrenaProxy v2.0\n\n[upscaler]\nmode = "<<c.up<<"\n\n[SGSR]\nenabled="<<c.en<<"\nmode="<<m[c.mode]<<"\nquality="<<q[c.qual]<<"\ncustom_scale="<<c.cscale<<"\nsharpness="<<c.sharp<<"\n\n[fsr2]\nquality_ratio = "<<c.fsr2r<<"\n\n[xess]\nquality_preset = "<<c.xp<<"\n\n[FrameGeneration]\nmode=x"<<(c.fg+1)<<"\nfps_threshold="<<c.fpst<<"\n\n[Overlay]\nenabled="<<c.ov<<"\nfps_display="<<c.fpsd<<"\nopacity="<<c.opa<<"\n\n[Advanced]\nforce_d3d11="<<c.fd11<<"\nmax_frame_queue="<<c.mfq<<"\nvsync="<<c.vs<<"\n";}

static ID3D11Device*gD=nullptr;static ID3D11DeviceContext*gC=nullptr;
static IDXGISwapChain*gS=nullptr;static ID3D11RenderTargetView*gR=nullptr;
static void MkRTV(){ID3D11Texture2D*b=nullptr;gS->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&b);if(b){gD->CreateRenderTargetView(b,nullptr,&gR);b->Release();}}
static void RmRTV(){if(gR){gR->Release();gR=nullptr;}}
static bool MkDev(HWND h){DXGI_SWAP_CHAIN_DESC d{};d.BufferCount=2;d.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;d.OutputWindow=h;d.SampleDesc.Count=1;d.Windowed=TRUE;d.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;D3D_FEATURE_LEVEL f;if(FAILED(D3D11CreateDeviceAndSwapChain(0,D3D_DRIVER_TYPE_HARDWARE,0,0,0,0,D3D11_SDK_VERSION,&d,&gS,&gD,&f,&gC)))return false;MkRTV();return true;}
static void RmDev(){RmRTV();if(gS)gS->Release();if(gC)gC->Release();if(gD)gD->Release();gS=nullptr;gC=nullptr;gD=nullptr;}
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);
static LRESULT WINAPI WP(HWND h,UINT m,WPARAM w,LPARAM l){if(ImGui_ImplWin32_WndProcHandler(h,m,w,l))return 1;if(m==WM_SIZE&&gD&&w!=SIZE_MINIMIZED){RmRTV();gS->ResizeBuffers(0,LOWORD(l),HIWORD(l),DXGI_FORMAT_UNKNOWN,0);MkRTV();return 0;}if(m==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(h,m,w,l);}

int WINAPI wWinMain(HINSTANCE hI,HINSTANCE,LPWSTR,int){
    WNDCLASSEXW wc={sizeof(wc),CS_CLASSDC,WP,0,0,hI,0,0,0,0,L"ACG",0};RegisterClassExW(&wc);
    HWND hw=CreateWindowExW(0,L"ACG",L"AdrenaProxy Config",WS_OVERLAPPEDWINDOW,100,100,520,580,0,0,hI,0);
    if(!MkDev(hw)){RmDev();return 1;}ShowWindow(hw,SW_SHOWDEFAULT);
    IMGUI_CHECKVERSION();ImGui::CreateContext();ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hw);ImGui_ImplDX11_Init(gD,gC);
    IniCfg cfg;{char p[MAX_PATH];GetModuleFileNameA(0,p,MAX_PATH);std::string s(p);auto x=s.find_last_of("\\/");cfg.path=(x!=std::string::npos?s.substr(0,x+1):"")+"adrena_proxy.ini";}Load(cfg);
    bool sv=false;float st=0;const float cl[4]={.06f,.06f,.08f,1};MSG msg{};
    while(msg.message!=WM_QUIT){
        if(PeekMessageW(&msg,0,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);continue;}
        ImGui_ImplDX11_NewFrame();ImGui_ImplWin32_NewFrame();ImGui::NewFrame();
        ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("##M",0,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoTitleBar);
        ImGui::TextColored({1,.42f,0,1},"ADRENAPROXY");ImGui::SameLine();ImGui::Text("Config Editor");ImGui::Separator();
        if(ImGui::BeginTabBar("T")){
        if(ImGui::BeginTabItem("Upscaler")){const char*u[]={"sgsr1","sgsr2","fsr2","xess"};int ui=0;for(int i=0;i<4;i++)if(!strcmp(cfg.up,u[i]))ui=i;if(ImGui::Combo("Plugin",&ui,u,4))strncpy(cfg.up,u[ui],31);ImGui::Checkbox("Enabled",&cfg.en);const char*sm[]={"Off","SGSR1","SGSR2"};ImGui::Combo("Mode",&cfg.mode,sm,3);const char*ql[]={"UltraQ","Quality","Balanced","Perf","UltraP"};ImGui::Combo("Quality",&cfg.qual,ql,5);ImGui::SliderFloat("Scale",&cfg.cscale,0,1,"%.2f");ImGui::SliderFloat("Sharp",&cfg.sharp,0,2,"%.2f");const float qs[]={.77f,.67f,.59f,.5f,.33f};ImGui::TextColored({.5f,.8f,1,1},"Effective: %.0f%%",(cfg.cscale>0?cfg.cscale:qs[cfg.qual])*100);ImGui::Separator();ImGui::SliderFloat("FSR2 Ratio",&cfg.fsr2r,0,3,"%.1f");ImGui::SliderInt("XeSS Preset",&cfg.xp,0,4);ImGui::EndTabItem();}
        if(ImGui::BeginTabItem("FrameGen")){const char*fg[]={"Off","x2","x3","x4"};ImGui::Combo("FG",&cfg.fg,fg,4);ImGui::SliderInt("FPS Thr",&cfg.fpst,0,240);ImGui::EndTabItem();}
        if(ImGui::BeginTabItem("Overlay")){ImGui::Checkbox("Enabled",&cfg.ov);ImGui::Checkbox("FPS",&cfg.fpsd);ImGui::SliderFloat("Opacity",&cfg.opa,.3f,1,"%.2f");ImGui::EndTabItem();}
        if(ImGui::BeginTabItem("Advanced")){ImGui::Checkbox("D3D11",&cfg.fd11);ImGui::SliderInt("FrameQ",&cfg.mfq,1,5);const char*vs[]={"Off","On","Adaptive"};int vi=cfg.vs;if(ImGui::Combo("VSync",&vi,vs,3))cfg.vs=vi;ImGui::EndTabItem();}
        ImGui::EndTabBar();}
        ImGui::Spacing();if(ImGui::Button("Save",{-1,30})){Save(cfg);sv=true;st=(float)ImGui::GetTime();}
        if(sv&&(float)ImGui::GetTime()-st<2.5f)ImGui::TextColored({.13f,.87f,.4f,1},"Saved!");
        if(ImGui::Button("Reload from disk",{-1,26})){Load(cfg);}
        ImGui::End();ImGui::Render();
        gC->OMSetRenderTargets(1,&gR,0);gC->ClearRenderTargetView(gR,cl);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());gS->Present(1,0);}
    ImGui_ImplDX11_Shutdown();ImGui_ImplWin32_Shutdown();ImGui::DestroyContext();RmDev();return 0;
}
