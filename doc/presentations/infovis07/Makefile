
all: ps 

ps: infovis07-igraph.ps

pdf: infovis07-igraph.pdf 

infovis07-igraph.ps: infovis07-igraph.dvi
	dvips -t a3 infovis07-igraph.dvi -o infovis07-igraph.ps

infovis07-igraph.pdf: infovis07-igraph.ps
	ps2pdf infovis07-igraph.ps

IMAGES=karate3d.eps screenshots.eps arch.eps RMKI-BME.eps CCSS.eps

%.png:: ../images/%.png
	cp $< $@

%.svg.gz:: ../images/%.svg.gz
	cp $< $@

karate3d.eps: karate3d.png
	convert karate3d.png karate3d.eps

screenshots.eps: screenshots.svg.gz degreedist.png clustering.png diameter.png \
	diameter2.png tkplot.png
	gzip -dc screenshots.svg.gz >screenshots.svg && \
	inkscape -E screenshots.eps -y 0.0 screenshots.svg

arch.eps: arch.svg.gz
	gzip -dc arch.svg.gz >arch.svg && \
	inkscape -E arch.eps -y 0.0 arch.svg

RMKI-BME.eps: RMKI-BME.svg.gz
	gzip -dc RMKI-BME.svg.gz >RMKI-BME.svg && \
	inkscape -E RMKI-BME.eps -y 0.0 RMKI-BME.svg

CCSS.eps: CCSS.svg.gz
	gzip -dc CCSS.svg.gz >CCSS.svg && \
	inkscape -E CCSS.eps -y 0.0 CCSS.svg

infovis07-igraph.dvi: infovis07-igraph.tex $(IMAGES)
	latex infovis07-igraph.tex

clean:
	rm -f karate3d.eps screenshots.eps arch.eps arch.eps_t \
	RMKI-BME.eps CCSS.eps infovis07-igraph.pdf infovis07-igraph.ps \
	infovis07-igraph.aux infovis07-igraph.dvi infovis07-igraph.log \
	screenshots.svg arch.svg{,.gz} RMKI-BME.svg{,.gz} CCSS.svg{,.gz} \
	clustering.png degreedist.png diameter.png diameter2.png \
	karate3d.png python3d.png tkplot.png


.PHONY: all ps pdf clean
