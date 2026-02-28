import time
import time
import streamlit as st
from rag_pipeline import run_pipeline

st.set_page_config(
    page_title="ClinIQ",
    page_icon="🏥",
    layout="wide"
)

if "case_description" not in st.session_state:
    st.session_state.case_description = ""
if "analysis_result" not in st.session_state:
    st.session_state.analysis_result = None
if "response_time" not in st.session_state:
    st.session_state.response_time = 0.0

EXAMPLE_CASES = [
    "50yr male, squeezing chest pain radiating to jaw",
    "23yr pregnant woman with burning urination",
    "8 month old boy, fussy and not feeding",
    "50yr woman with esophageal varices and confusion",
]


def set_example_case(example: str):
    st.session_state.case_description = example


def render_confidence_banner(confidence: str):
    if confidence == "high":
        st.success("✅ High Confidence — Strong evidence retrieved")
    elif confidence == "medium":
        st.warning("⚠️ Medium Confidence — Moderate evidence")
    else:
        st.error("❌ Low Confidence — Limited matching evidence")


with st.sidebar:
    st.header("Status")
    st.success("Endee Vector DB — Connected")
    st.info("🤖 LLM: Llama 3.2 3B (Local)")
    st.write("📚 Datasets: MedQA USMLE + Lavita Medical QA")
    st.header("Pipeline")
    st.markdown("""
1. 🔄 Query Rewriting
2. 🧪 HyDE Generation
3. 🔍 Vector Retrieval via Endee
4. 🤖 Grounded Generation
    """)
    st.caption("ClinIQ is for educational exploration only and must not guide real-world medical decisions.")

st.title("🏥 ClinIQ")
st.subheader("Evidence-Grounded Clinical Decision Support")

badge_col1, badge_col2 = st.columns(2)
with badge_col1:
    st.metric("📚 MedQA USMLE", "1,000 records")
with badge_col2:
    st.metric("📚 Lavita Medical QA", "1,000 records")
st.caption("🗄️ 2,000+ Medical Cases Indexed in Endee")

st.write("### Clinical Case Intake")
st.text_area(
    "Describe the patient case...",
    key="case_description",
    height=200,
    placeholder="Describe the patient case..."
)

st.write("#### Try an example")
example_rows = [st.columns(2), st.columns(2)]
for row_idx, cols in enumerate(example_rows):
    for col_idx, col in enumerate(cols):
        example = EXAMPLE_CASES[row_idx * 2 + col_idx]
        if col.button(example, use_container_width=True):
            set_example_case(example)

analyze_clicked = st.button("🔍 Analyze Case", type="primary", use_container_width=True)

if analyze_clicked:
    clinical_query = st.session_state.case_description.strip()
    if not clinical_query:
        st.error("Please describe the patient case before running the pipeline.")
    else:
        start = time.time()
        with st.spinner("Running ClinIQ pipeline: rewrite → HyDE → retrieval → generation"):
            result = run_pipeline(clinical_query)
        duration = time.time() - start
        st.session_state.analysis_result = result
        st.session_state.response_time = duration

if st.session_state.analysis_result:
    result = st.session_state.analysis_result
    duration = st.session_state.response_time
    st.caption(f"⏱️ Completed in {duration:.2f}s")
    render_confidence_banner(result["confidence"])

    st.subheader("📋 Clinical Analysis")
    st.write(result["diagnosis"])

    with st.expander("🔍 Pipeline Transparency", expanded=False):
        st.markdown("**Rewritten Query**")
        st.write(result.get("rewritten_query", "N/A"))
        st.markdown("**HyDE Hypothesis**")
        st.write(result.get("hypothesis", "N/A"))
        st.markdown("**Retrieved Cases**")
        st.write(f"{len(result['sources'])} neighbors from Endee")

    st.subheader("📚 Retrieved Evidence from Endee")
    for source in result["sources"]:
        similarity = max(0.0, min(source.get("similarity", 0), 1.0))
        st.write(f"**Case {source['case']}** · {similarity * 100:.1f}% similarity")
        st.caption(source.get("source", "Unknown Source"))
        st.write("*Question*")
        st.write(source.get("question", ""))
        st.write("*Answer*")
        st.write(source.get("answer", ""))
        st.progress(similarity)

st.divider()
st.markdown("**ClinIQ | Endee Vector DB · Llama 3.2 · all-MiniLM-L6-v2**")
st.caption("⚠️ For educational purposes only")