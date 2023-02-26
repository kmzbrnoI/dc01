all: fab

clean:
	rm -r fab

fab: dc01.kicad_pcb dc01.kicad_sch
	kikit fab jlcpcb --no-drc --assembly --schematic dc01.kicad_sch dc01.kicad_pcb fab

.PHONY: all clean
