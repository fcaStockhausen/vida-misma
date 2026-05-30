#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SECTIONS_DIR="$SCRIPT_DIR/secciones"
OUTPUT_DIR="$SCRIPT_DIR"
mkdir -p "$OUTPUT_DIR"

# Use brew pandoc (3.9.0.2) over anaconda's older version
PANDOC="/opt/homebrew/bin/pandoc"

# All section files in order
SECTIONS=(
  "$SECTIONS_DIR/00_metadata.yaml"
  "$SECTIONS_DIR/01_automatas_celulares.md"
  "$SECTIONS_DIR/02_generacion_procedimental.md"
  "$SECTIONS_DIR/03_agentes_ia.md"
  "$SECTIONS_DIR/04_pathfinding.md"
  "$SECTIONS_DIR/05_sistemas_fisicos.md"
  "$SECTIONS_DIR/06_simulacion_social.md"
  "$SECTIONS_DIR/07_principios_diseno.md"
  "$SECTIONS_DIR/08_algoritmos_complementarios.md"
  "$SECTIONS_DIR/09_stack_tecnologico.md"
  "$SECTIONS_DIR/10_papers_arxiv.md"
  "$SECTIONS_DIR/11_part2_intro.md"
  "$SECTIONS_DIR/12_factory.md"
  "$SECTIONS_DIR/13_director.md"
  "$SECTIONS_DIR/14_inhabitants.md"
  "$SECTIONS_DIR/15_spaces.md"
  "$SECTIONS_DIR/16_social_fabric.md"
  "$SECTIONS_DIR/17_life_death.md"
  "$SECTIONS_DIR/18_architecture.md"
  "$SECTIONS_DIR/19_pipelines.md"
  "$SECTIONS_DIR/20_adaptive_logistics.md"
  "$SECTIONS_DIR/references.yaml"
)

COMMON_FLAGS=(
  --filter pandoc-crossref
  --citeproc
  --bibliography="$SECTIONS_DIR/references.yaml"
  --number-sections
  --toc
  -M cref=true
  -M autoSectionLabels=true
)

echo "=== Compiling document with pandoc + crossref ==="

"$PANDOC" "${SECTIONS[@]}" \
  "${COMMON_FLAGS[@]}" \
  --pdf-engine=xelatex \
  -o "$OUTPUT_DIR/bases_matematicas.pdf"

echo "=== PDF generated: $OUTPUT_DIR/bases_matematicas.pdf ==="

"$PANDOC" "${SECTIONS[@]}" \
  "${COMMON_FLAGS[@]}" \
  --standalone \
  --mathjax \
  -o "$OUTPUT_DIR/bases_matematicas.html"

echo "=== HTML generated: $OUTPUT_DIR/bases_matematicas.html ==="
echo "=== Build complete ==="
