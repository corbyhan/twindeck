"""Creates build/demo-files (sample folders, documents and generated pictures) and build/demo (a portable
TwinDeck with its own settings) for README screenshots, so no personal files ever show up.

usage: python tools/make_demo.py
"""
import json
import math
import os
import random
import shutil
import time

from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
FILES = os.path.join(ROOT, "build", "demo-files")
APP = os.path.join(ROOT, "build", "demo")
rnd = random.Random(7)


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def landscape(path, sky_top, sky_bottom, sun, layers, size=(1600, 1000)):
    w, h = size
    img = Image.new("RGB", size)
    d = ImageDraw.Draw(img)
    for y in range(h):
        d.line([(0, y), (w, y)], fill=lerp(sky_top, sky_bottom, y / h))
    if sun:
        glow = Image.new("RGBA", size, (0, 0, 0, 0))
        gd = ImageDraw.Draw(glow)
        cx, cy, r, col = sun
        gd.ellipse([cx - r * 3, cy - r * 3, cx + r * 3, cy + r * 3], fill=col + (60,))
        glow = glow.filter(ImageFilter.GaussianBlur(r))
        img.paste(glow, (0, 0), glow)
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=col)
    for base, amp, col in layers:
        pts = [(0, h)]
        phase = rnd.random() * 10
        for x in range(0, w + 20, 20):
            y = base + amp * (0.6 * math.sin(x / 170 + phase) + 0.4 * math.sin(x / 61 + phase * 2)) + rnd.randint(-6, 6)
            pts.append((x, y))
        pts.append((w, h))
        d.polygon(pts, fill=col)
    img.save(path, quality=90)


def write(path, text="", size=0, days_ago=0):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        if text:
            f.write(text.encode("utf-8"))
        elif size:
            f.write(os.urandom(size))
    t = time.time() - days_ago * 86400 - rnd.randint(0, 80000)
    os.utime(path, (t, t))


def build_files():
    shutil.rmtree(FILES, ignore_errors=True)
    pics = os.path.join(FILES, "사진")
    os.makedirs(pics)
    scenes = [
        ("제주 노을.jpg", (255, 140, 90), (120, 60, 140), (800, 560, 70, (255, 220, 150)),
         [(620, 60, (70, 40, 90)), (720, 40, (40, 25, 60))]),
        ("설악산 아침.jpg", (150, 200, 255), (230, 240, 255), (1200, 220, 50, (255, 250, 220)),
         [(520, 120, (90, 120, 150)), (640, 90, (60, 90, 110)), (760, 60, (35, 60, 70))]),
        ("한강 야경.jpg", (10, 15, 40), (40, 30, 90), (300, 200, 30, (240, 240, 210)),
         [(700, 30, (20, 20, 45)), (780, 15, (10, 10, 25))]),
        ("가을 숲.jpg", (250, 200, 120), (250, 150, 80), None,
         [(500, 90, (190, 90, 40)), (620, 70, (140, 60, 30)), (760, 40, (80, 35, 20))]),
        ("겨울 바다.jpg", (180, 210, 235), (100, 150, 200), (1300, 300, 40, (255, 255, 255)),
         [(700, 20, (60, 110, 160)), (800, 10, (40, 80, 130))]),
        ("벚꽃길.jpg", (255, 215, 230), (255, 240, 245), None,
         [(560, 80, (240, 160, 190)), (700, 60, (200, 110, 150)), (820, 30, (120, 70, 90))]),
        ("남해 섬.jpg", (90, 180, 230), (200, 235, 250), (400, 180, 45, (255, 250, 230)),
         [(640, 50, (40, 120, 90)), (760, 25, (20, 80, 110))]),
        ("오로라.jpg", (5, 20, 40), (20, 90, 90), None,
         [(650, 70, (15, 30, 40)), (760, 40, (5, 15, 25))]),
    ]
    for i, (name, top, bottom, sun, layers) in enumerate(scenes):
        landscape(os.path.join(pics, name), top, bottom, sun, layers)
        t = time.time() - (i * 9 + 3) * 86400
        os.utime(os.path.join(pics, name), (t, t))
    docs = os.path.join(FILES, "문서")
    write(os.path.join(docs, "회의록_10월.txt"), """# 10월 정기 회의록

일시: 2026-10-14 (수) 14:00
참석: 기획팀, 개발팀, 디자인팀

## 안건
1. 4분기 출시 일정 확정
2. 사용자 의견 반영 (초성 검색, 분할 화면)
3. 설치 프로그램 개선

## 결정 사항
- 11월 첫째 주 베타 배포
- 한글 파일 이름 정렬은 자연 정렬을 기본으로
- 다음 회의: 10월 28일
""", days_ago=2)
    write(os.path.join(docs, "2026 사업계획서.docx"), size=48213, days_ago=12)
    write(os.path.join(docs, "4분기 예산안.xlsx"), size=23110, days_ago=5)
    write(os.path.join(docs, "제품 소개 발표.pptx"), size=812344, days_ago=20)
    write(os.path.join(docs, "보고서 초안.pdf"), size=301122, days_ago=1)
    write(os.path.join(docs, "고객 설문 결과.csv"), "이름,점수,의견\n김하늘,5,빨라요\n이도윤,4,초성 검색이 편해요\n", days_ago=7)
    write(os.path.join(docs, "계약서", "용역 계약서.pdf"), size=152003, days_ago=40)
    write(os.path.join(docs, "계약서", "비밀유지 서약서.pdf"), size=88004, days_ago=41)
    write(os.path.join(docs, "영수증", "2026-09 교통비.png"), size=40211, days_ago=30)
    write(os.path.join(docs, "영수증", "2026-10 식대.png"), size=51022, days_ago=3)
    proj = os.path.join(FILES, "프로젝트")
    write(os.path.join(proj, "README.md"), "# 사내 도구\n\n빌드: `npm run build`\n", days_ago=4)
    write(os.path.join(proj, "src", "main.ts"), "export function main() {\n  console.log('안녕하세요');\n}\n", days_ago=1)
    write(os.path.join(proj, "src", "util.ts"), "export const sum = (a: number, b: number) => a + b;\n", days_ago=6)
    write(os.path.join(proj, "package.json"), '{\n  "name": "tools",\n  "version": "1.2.0"\n}\n', days_ago=4)
    write(os.path.join(proj, "설계 메모.md"), "## 구조\n- 화면\n- 데이터\n", days_ago=9)
    write(os.path.join(FILES, "음악", "산책할 때.mp3"), size=4021133, days_ago=60)
    write(os.path.join(FILES, "음악", "비 오는 날.mp3"), size=3810022, days_ago=55)
    write(os.path.join(FILES, "다운로드", "설치 파일.zip"), size=10233400, days_ago=2)
    write(os.path.join(FILES, "다운로드", "한글 글꼴 모음.zip"), size=24400122, days_ago=15)
    write(os.path.join(FILES, "할 일 목록.txt"), "- 우유 사기\n- 보고서 마무리\n- 운동\n", days_ago=0)
    write(os.path.join(FILES, "한글문서.txt"), "한글 파일 이름도 초성(ㅎㄱ)으로 찾을 수 있습니다.\n", days_ago=1)


def build_app():
    os.makedirs(APP, exist_ok=True)
    shutil.copy2(os.path.join(ROOT, "build", "TwinDeck.exe"), os.path.join(APP, "TwinDeck.exe"))
    # portable mode: TwinDeck-Config.json next to the exe keeps every setting inside build/demo
    cfg = {"Language": 2, "ColorScheme": 1, "FontName": "Segoe UI", "FontSize": 10.0, "SetupPrompted": True,
           "ShowRecents": True, "AlternatingRowColors": False}
    json.dump(cfg, open(os.path.join(APP, "TwinDeck-Config.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=2)
    user = {"PathBookmarks": [os.path.join(FILES, "문서"), os.path.join(FILES, "프로젝트")],
            "Recents": [{"Path": os.path.join(FILES, n), "Score": 5 - i, "Decay": int(time.time())}
                        for i, n in enumerate(["사진", "문서", "프로젝트", "다운로드"])]}
    json.dump(user, open(os.path.join(APP, "TwinDeck-UserData.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=2)


if __name__ == "__main__":
    build_files()
    build_app()
    print("demo files:", FILES)
    print("demo app:", APP)
