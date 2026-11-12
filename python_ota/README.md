# בניית FW ועדכון OTA מהמחשב

התיקייה מפעילה את תהליך הבנייה ואת פרוטוקול OTA v2 הקיים בפרויקט.
אין צורך לפתוח את STM32CubeIDE או את האתר כדי לבנות ולהעלות עדכון.

## הפעלה מהירה

1. התקן פעם אחת **Python 3.10 ומעלה** עם `pip`, `venv` ו־Python Launcher
   (מומלץ Python 3.13 בגרסת 64 ביט), ואת **STM32CubeIDE** עם קומפיילר ARM.
   לצורך העלאה נדרשים Windows 11 ומתאם Bluetooth התומך ב־BLE.
2. לחץ פעמיים על **`build_ota.bat`**. הוא יוצר סביבת Python מקומית, מתקין את
   הספריות החסרות, בודק את התלויות והקומפיילר, מקמפל את ה־bootloader ואת
   האפליקציה, מאמת את מפת הזיכרון ואורז קובץ עדכון.
3. בסיום מופיע הנתיב המלא של **`artifacts/ota/NatiLab-<version>-<CRC32>.ota.bin`**.
   זה הקובץ להעלאה, גם דרך האתר. `latest.json` מצביע על הבנייה המוצלחת האחרונה.
4. נתק את האתר/הטלפון מהבקר, הפעל Bluetooth ולחץ פעמיים על **`update_ota.bat`**.
   הכלי טוען ומאמת את הקובץ האחרון, סורק בקרים ומאפשר בחירה אם נמצאו כמה.
   לאחר הצגת הבקר והודעת המחיקה, הקלד **`UPDATE`** כדי להתחיל.
5. הכלי מציג התקדמות, מבקש מהבקר לבדוק את הקובץ, מתקין ומתחבר מחדש.
   הצלחה מתקבלת רק כאשר הגרסה, הגודל וה־CRC של האפליקציה שרצה תואמים לקובץ.

**העדכון מוחק את כל התמונות והמסלולים השמורים בבקר, גם אם ההעברה מבוטלת.**
ייצא קבצים שברצונך לשמור לפני תחילת העדכון. ביטול לפני אישור `UPDATE` אינו
משנה את הבקר. אחרי שליחת INSTALL יש לבדוק את הגרסה לאחר האתחול; ניתוק כשלעצמו
אינו הוכחת הצלחה.

אפשר לפתוח גם **`run.bat`**, המציג תפריט בנייה, העלאה, סריקה ובדיקת הסביבה.
קובץ `build_ota.bat` בשורש ה־FW מפנה לתהליך החדש.

## מה מותקן ומה נבדק

- הספריות שב־`requirements.txt` מותקנות אוטומטית לתוך `python_ota/.venv`;
  סביבת Python הכללית במחשב אינה משתנה. גישה לאינטרנט נדרשת בהתקנה הראשונה
  או בשינוי התלויות. בהרצות הבאות נבדקים imports ו־`pip check` ללא עדכון אוטומטי.
- Python ו־STM32CubeIDE עצמם צריכים להיות מותקנים מראש. אם חסרים, הכלי מציג
  הנחיות וקישור. אין התקנה שקטה של IDE או מנהלי התקנים.
- זיהוי CubeIDE מחפש ב־PATH, ב־`C:\ST` וב־`%ProgramFiles%\ST`, ומעדיף גרסה חדשה.
  נבדקים קובצי ההתקנה והפעלת `arm-none-eabi-gcc` ו־`arm-none-eabi-nm`.
- הבנייה משתמשת ב־`BLE_ApplicationInstallManager/Release` וב־`BleServoController/Debug`,
  כמו תהליך הבנייה הקיים. מצב Eclipse נשמר ב־`artifacts/python-ota`.
- לפני כל build מוסרים רק ה־BIN וה־ELF של אותו יעד, כדי שלא ניתן יהיה לארוז תוצר
  ישן אם הקומפילציה נכשלה או לא ייצרה פלט. נבדקים קוד היציאה ושגיאות ביומן.
- לאחר הבנייה רץ `tools/validate_firmware.py`, ובאריזה נבדקים גודל, כתובת,
  וקטורים, תג סיום ו־CRC. קובץ פגום או manifest שאינו תואם נדחים לפני Bluetooth.
- הבנייה אינה צורבת חומרה. אין שינוי אוטומטי של מספר הגרסה: ערוך את
  `Core/Inc/firmware_version.h` לפני בנייה אם ברצונך לתת לגרסה שם חדש.
- אפשר להעלות גרסה ישנה, חדשה או זהה; אין חסימת downgrade.

## פקודות מתוך שורש ה־FW

```powershell
# תפריט ובדיקת התקנות
.\python_ota\run.bat
.\python_ota\run.bat check

# קומפילציה ואריזה; נתיב IDE מפורש הוא אופציונלי
.\python_ota\build_ota.bat
.\python_ota\build_ota.bat --cubeide-root "C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE"

# סריקה וקריאת גרסה בלבד
.\python_ota\update_ota.bat --scan
.\python_ota\update_ota.bat --version --name NL802915

# בחירת קובץ ובקר; ללא --erase-files מתבקש אישור אינטראקטיבי
.\python_ota\update_ota.bat --file "C:\firmware\release.ota.bin" --name NL802915

# העלאה אוטומטית, עם אישור מפורש למחיקת קובצי הבקר
.\python_ota\update_ota.bat --latest --address "00:80:E1:2A:26:24" --erase-files

# בדיקה מקומית בלבד
.\python_ota\update_ota.bat --check-file "C:\firmware\release.ota.bin"
```

השמות והכתובות בדוגמאות הם דוגמאות; השתמש בתוצאות הסריקה של הבקר הרצוי.
אם עובדים מתוך סקריפט, הגדר `$env:OTA_NO_PAUSE='1'` כדי למנוע את ההמתנה
למקש בסוף ה־BAT. שגיאה מחזירה exit code שונה מאפס. אפשר גם להגדיר
`STM32CUBEIDE_ROOT`; הארגומנט `-CubeIdeRoot` של ה־BAT הישן עדיין נתמך.
יש להימנע מעריכת מקורות או מבנייה נוספת ב־IDE במקביל לבנייה זו.

אחרי הכנת הסביבה ניתן להריץ ישירות, ללא BAT וללא התקנה מחדש:

```powershell
.\python_ota\.venv\Scripts\python.exe python_ota/build_ota.py --help
.\python_ota\.venv\Scripts\python.exe python_ota/update_ota.py --help
.\python_ota\.venv\Scripts\python.exe -m unittest discover -s python_ota -p test_workflow.py -v
```

בדיקות ה־Python מדמות בחירה, ביטול, שגיאות build, קבצים פגומים ואימות לאחר
אתחול; הן אינן מתחברות לבקר. בדיקות C נוספות של ה־receiver וה־bootloader
נמצאות ב־`tools/check_ota.py` ודורשות את קומפיילר C של Visual Studio.

## לוגים ופתרון תקלות

- יומני קומפילציה, `validation.log` ויומני `ble-*.log` נשמרים ב־`artifacts/ota`.
  שגיאת build משאירה את `latest.json` של הבנייה הקודמת; אין להתייחס אליה כבנייה חדשה.
- אם לא נמצא בקר, בדוק חשמל ו־Bluetooth ונתק לקוחות BLE אחרים. `Access Denied`
  ב־Windows עשוי לדרוש סגירת דפדפן/אפליקציה שמחזיקים חיבור, ואז ניסיון נוסף.
- אם `pip` נכשל, בדוק אינטרנט/Proxy ושה־Python תומך בחבילות Windows המותקנות.
  אם הזזת את הפרויקט או הסרת את Python, אפשר למחוק **רק** את `python_ota/.venv`
  ולתת ל־BAT ליצור אותה מחדש.
- בבקר ללא ה־bootloader והאפליקציה התומכים ב־OTA v2 נדרשת התקנה ראשונית ב־ST-LINK.
  כלי זה אינו מבצע התקנה ראשונית או שינוי bootloader דרך BLE. ראה [OTA.md](../OTA.md).
- `--chunk 16` מאפשר העברה עם מנות קטנות יותר אם מתאם ה־BLE מחייב זאת.

מקורות לתלות החיצונית: [Bleak 3.1.0 — דרישות והתקנה](https://pypi.org/project/bleak/3.1.0/),
[Windows backend](https://bleak.readthedocs.io/en/stable/backends/windows.html).

## אימות מקומי — 12 בנובמבר 2026

ההתקנה בסביבה מבודדת, הבנייה דרך ה־BAT, בדיקות מפת הזיכרון ו־16 בדיקות
Python עברו. נבנה `NatiLab-1.1.0-ota.3-8B6C6DD5.ota.bin` עם 352816 בתים
באפליקציה. נשארו שלוש אזהרות קומפיילר קיימות ב־`stm32_lpm_if.c` וב־`app_ble.c`.
ה־BAT החדש סרק בפועל את `NL802915` וקרא ממנו בהצלחה
`OK VERSION 1.1.0-ota.2 IMAGE=F8BB9BAC SIZE=351952 OTA=2`.
לא בוצעה העלאה או מחיקת קבצים בבקר במסגרת בדיקת הכלים החדשים.
