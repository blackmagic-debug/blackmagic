OUT = blackmagic_mini

.PHONY: all clean
all: $(OUT).gbr.zip $(OUT).xy

%.net: %.sch
	gnetlist -g PCB -o $@ $<

%.xy %.bom: %.pcb
	pcb -x bom $<

%.gbr: %.pcb
	mkdir -p $@
	pcb -x gerber --gerberfile $@/$(basename $@) $<
	touch $@

%.gbr.zip: %.gbr
	zip -r $@ $<

clean:
	-rm -rf *.bom *.xy *.net *.gbr.zip $(OUT).gbr

