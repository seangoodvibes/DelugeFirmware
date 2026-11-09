"""The diagnostic log must cover a full rebuild even with one-frame callbacks."""
from pathlib import Path
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent

class ProbeTests(unittest.TestCase):
    def test_small_callbacks_do_not_truncate_diagnostics(self):
        with tempfile.TemporaryDirectory(prefix='freeze-probe-test-') as temp:
            root = Path(temp)
            (root/'definitions.h').write_text('#define PLACE_SDRAM_BSS\n')
            (root/'test.cpp').write_text(f'''
#include "{HERE / 'stubs.h'}"
#include "{HERE / 'phase_probe.h'}"
int main() {{
 freezeProbe.reset();
 StereoSample frame{{1000,-1000}};
 float bands[2][4]{{{{1,0,0,0}},{{1,0,0,0}}}};
 const int count=33*44100;
 for(int i=0;i<count;++i) {{
  freezeProbe.capture({{&frame,1}},{{&frame,1}},i%352800);
  freezeProbe.observe(0,1,4,bands,{{&frame,1}},{{&frame,1}});
 }}
 assert(freezeProbe.count==count && freezeProbe.blockCount==count);
 assert(freezeProbe.observedSamples==count && freezeProbe.rejectedSamples[0]==count);
 assert(freezeProbe.passedSamples==0 && freezeProbe.rowCount<4000);
 assert(freezeProbe.rows[0].correlation>.99f);
 for(int i=0;i<count;++i) assert(freezeProbe.blockSizes[i]==1);
 freezeProbe.reset();
 assert(freezeProbe.count==0 && freezeProbe.observedSamples==0 && freezeProbe.blockCount==0);
}}
''')
            subprocess.run(['clang++','-std=c++20','-O1','-fsanitize=address,undefined',
                            '-I',str(root),str(root/'test.cpp'),'-o',str(root/'test')],check=True)
            subprocess.run([str(root/'test')],check=True)

if __name__=='__main__':unittest.main()
