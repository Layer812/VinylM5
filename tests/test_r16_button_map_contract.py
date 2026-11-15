from pathlib import Path
p=Path(__file__).resolve().parents[1]
m=(p/'src/main.cpp').read_text()
assert 'if (rightClicks.singleDue(keyNow))' in m
assert 'if (leftClicks.singleDue(keyNow)) changeTrackOrFolder(true);' in m
assert 'M5.BtnB.wasReleased() && rightClicks.onRelease(keyNow)' in m
assert 'M5.BtnA.wasReleased() && leftClicks.onRelease(keyNow)' in m
assert 'changeTrackOrFolder(false);       // RIGHT double: next sequential track' in m
assert 'changeTrackOrFolder(false,true);  // LEFT double: random in same folder' in m
assert 'lastRightRelease' not in m
assert m.count('M5.begin(cfg);')==1
print('PASS R16 FIX1 source contract: RIGHT double = next track, LEFT double = same-folder random, singles preserved')
