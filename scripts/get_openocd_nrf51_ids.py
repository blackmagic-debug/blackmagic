#!/usr/bin/python

"""Pulls nRF51 IDs from openocd's nrf51.c in a form suitable for
pasting into blackmagic's nrf51.c

"""

import subprocess,re

cmd = 'git archive --remote=git://git.code.sf.net/p/openocd/code HEAD src/flash/nor/nrf51.c | tar -xO'

class Spec():
    def __repr__(self):
        return "0x%04X: /* %s %s %s */"%(self.hwid,self.comment, self.variant,self.build_code)

fd = subprocess.Popen(cmd,shell=True,stdout=subprocess.PIPE).stdout

specdict={}
specs=[]
spec=Spec()
for line in fd.read().split('\n'):
    m=re.search('/\*(.*)\*/',line)
    if m:
        lastcomment=m.group(1)

    m=re.search('.hwid.*=\s*(0x[0-9A-F]*),',line)
    if m:
        spec.hwid=int(m.group(1),base=0)
    m=re.search('.variant.*=\s*"(.*)",',line)
    if m:
        spec.variant=m.group(1)
    m=re.search('.build_code.*=\s*"(.*)",',line)
    if m:
        spec.build_code=m.group(1)
    m=re.search('.flash_size_kb.*=\s*([0-9]*),',line)
    if m:
        spec.flash_size_kb=int(m.group(1),base=0)
        ram,flash = {'AA':(16,256),
                     'AB':(16,128),
                     'AC':(32,256)}[spec.variant[-2:]]
        assert flash==spec.flash_size_kb
        spec.ram_size_kb = ram
        nicecomment =lastcomment.strip().replace('IC ','').replace('Devices ','').replace('.','')
        spec.comment=nicecomment

        specdict.setdefault((ram,flash),[]).append(spec)
        specs.append(spec)
        spec=Spec()

for (ram,flash),specs in specdict.iteritems():
    specs.sort(key=lambda x:x.hwid)
    for spec in specs:
        print "\tcase",spec
    print '\t\tt->driver = "Nordic nRF51";'
    print '\t\ttarget_add_ram(t, 0x20000000, 0x%X);'%(1024*ram)
    print '\t\tnrf51_add_flash(t, 0x00000000, 0x%X, NRF51_PAGE_SIZE);'%(1024*flash)
    print '\t\tnrf51_add_flash(t, NRF51_UICR, 0x100, 0x100);'
    print '\t\ttarget_add_commands(t, nrf51_cmd_list, "nRF51");'
    print '\t\treturn true;'

