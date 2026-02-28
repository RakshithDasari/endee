[![Python](https://img.shields.io/badge/Python-3.12-blue)](https://www.python.org/) [![Streamlit](https://img.shields.io/badge/Streamlit-Latest-FF4B4B)](https://streamlit.io/) [![Ollama](https://img.shields.io/badge/Ollama-Llama%203.2%203B-000000)](https://ollama.ai) [![Endee](https://img.shields.io/badge/Vector%20DB-Endee-4F46E5)](https://github.com/endee-io/endee) [![License](https://img.shields.io/badge/License-Apache%202.0-green)](https://github.com/endee-io/endee/blob/main/LICENSE)

# ClinIQ: Evidence-Grounded Clinical Q&A

ClinIQ is a medical question-answering assistant designed to demonstrate how a carefully engineered Retrieval-Augmented Generation (RAG) pipeline keeps large language models honest. The application was created during a clinical AI evaluation cycle, so every decision favors clarity, reproducibility, and explainability.

Instead of relying on free-form model outputs, ClinIQ anchors each answer to medical exam cases stored inside Endee. The system embeds more than 2,000 MedQA and Lavita records, keeps everything local, and cites its sources while admitting uncertainty whenever the evidence is thin. Think of it as a polite clinician-in-training that always shows its work.

---

## Why this project exists

- Prove that Endee can operate as a trustworthy local vector store for healthcare data.
- Show that RAG techniques (query rewriting, HyDE, similarity search, grounded generation) dramatically reduce hallucinations.
- Provide a UI anyone can run during a review or hiring loop without cloud dependencies.

---

## How Endee fits in

Endee is the backbone of ClinIQ. I run it locally via Docker, create an index called `cliniq`, and store 2,000+ medical cases as vectors with rich metadata. When a new question comes in, I embed it, search Endee using cosine similarity, and hand those matched cases to the LLM. No Endee, no ClinIQ. Here’s what it looks like in code:

```python
# Connect to Endee
client = Endee()
client.set_base_url("http://localhost:8080/api/v1")

# Create index
client.create_index(
    name="cliniq",
    dimension=384,
    space_type="cosine",
    precision="float32"
)

# Store medical cases as vectors
index.upsert([{
    "id": "med_001",
    "vector": embedding,
    "meta": {"question": "...", "answer": "...", "source": "MedQA"}
}])

# Search semantically
results = index.query(vector=query_embedding, top_k=5)
```

Under the hood:

- **Local-first:** Endee runs as a Docker service on `localhost:8080`, so all data stays on-device.
- **Purpose-built index:** The 384-dimension configuration mirrors the all-MiniLM-L6-v2 embedding size.
- **Rich metadata:** Each vector keeps `question`, `answer`, `source`, and `exam_type` to power citations.
- **Cosine similarity search:** Queries are embedded and compared against the collection with cosine distance.
- **Evidence pipeline:** Retrieved cases feed directly into Llama 3.2 through Ollama.

---

## How the RAG pipeline works

Instead of dumping a question straight into an LLM, ClinIQ follows four disciplined steps:

1. **Query Rewriting** – Llama 3.2 reformulates layperson language into structured clinical phrasing, improving recall.
2. **HyDE (Hypothetical Document Embeddings)** – The model drafts a hypothetical case, which better represents textbook wording for embedding.
3. **Vector Retrieval** – The HyDE text is embedded (384 floats) and searched against Endee with cosine similarity, returning the top five cases.
4. **Grounded Generation** – Only the retrieved cases are supplied back to Llama 3.2 for a cited answer plus a confidence flag.

---

## Why this matters

- **Source-backed answers:** Every response carries citations so reviewers can jump into the original MedQA or Lavita text.
- **Graceful uncertainty:** Weak similarity scores simply surface a caution banner instead of confident guesses.
- **Evaluation-friendly:** Everything runs locally, which keeps the review loop private and repeatable.

---

## Datasets used

- **MedQA USMLE (GBaker/MedQA-USMLE-4-options)** – 1,000 records pulled from real U.S. medical licensing questions.
- **Lavita Medical QA (lavita/medical-qa-datasets)** – 1,000 conversational question-answer pairs covering common scenarios.
- **Total:** 2,000+ cases living inside the Endee index, each tagged with its origin.

---

## Tech stack

| Component | Technology |
|---|---|
| Vector Database | Endee (endee-io/endee) |
| LLM | Llama 3.2 3B via Ollama |
| Embeddings | all-MiniLM-L6-v2 |
| UI | Streamlit |
| Language | Python 3.12 |

---

## Project structure

```
examples/cliniq/
├── app.py            → Streamlit web interface
├── rag_pipeline.py   → Core RAG logic (rewrite + HyDE + retrieve + generate)
├── ingest.py         → Load datasets, embed, store in Endee
├── requirements.txt  → Python dependencies
├── docker-compose.yml→ Runs Endee locally
└── README.md
```

---

## Setup instructions (quick run)

### Prerequisites
- **Docker Desktop** – spins up Endee locally.
- **Python 3.10+** – runs the ingestion script, pipeline, and UI.
- **Ollama** – hosts the Llama 3.2 model locally so no tokens leave your laptop.

### Step 1: Fork and clone
```bash
git clone https://github.com/YOUR_USERNAME/endee
cd endee/examples/cliniq
```

### Step 2: Start the Endee database
```bash
docker compose up -d
# Visit http://localhost:8080 to confirm Endee is running
```

### Step 3: Set up Python environment
```bash
python -m venv venv
venv\Scripts\activate   # Windows
source venv/bin/activate # Mac/Linux
pip install -r requirements.txt
```

### Step 4: Pull the local LLM
```bash
ollama pull llama3.2:3b
# Downloads ~2GB model that runs fully offline
```

### Step 5: Load medical data into Endee
```bash
python ingest.py
# Embeds 2000+ cases and stores them in Endee (≈10 minutes)
```

### Step 6: Launch ClinIQ
```bash
streamlit run app.py
# Open http://localhost:8501 in your browser
```

---

## Example usage

Start from the example prompts inside the Streamlit sidebar or try cases like:

1. **“50yr male, squeezing chest pain radiating to jaw”** – Retrieves ischemic heart disease examples plus emergency considerations.
2. **“23yr pregnant woman with burning urination”** – Surfaces obstetric-safe UTI guidance.
3. **“8 month old boy, fussy and not feeding”** – Highlights pediatric dehydration assessments.

The answer panel lists sources, similarity scores, and a color-coded confidence chip.

---

## Design decisions

1. **Endee over hosted vectors** – Keeps clinical content private, highlights native Endee APIs, and avoids rate limits during reviews.
2. **Local LLM through Ollama** – Makes demos reproducible without external tokens.
3. **HyDE + rewriting** – Bridges the gap between natural symptom descriptions and exam-style language.
4. **Minimal dependencies** – Staying close to the Endee SDK makes the reasoning traceable for evaluators.

---

## Next steps

- Expand the index beyond 10k cases for deeper coverage.
- Accept clinician-provided PDFs and automatically chunk + embed them.
- Explore agentic RAG loops that self-critique generated answers.
- Deploy Endee to a lightweight cloud VM for collaborative testing.
- Experiment with GraphRAG for multi-hop reasoning.

---

## Concepts highlighted

- **Vector databases (Endee):** Dense storage, cosine search, metadata filtering.
- **Sentence embeddings (all-MiniLM-L6-v2):** 384-dimensional representations of medical text.
- **Query rewriting + HyDE:** Structured prompts that boost retrieval quality.
- **Grounded generation:** Llama 3.2 responses constrained by cited evidence.
- **Local-first deployment:** Docker + Ollama keep sensitive data offline.

These building blocks collectively satisfy the evaluation brief while remaining accessible to anyone opening the repo for the first time.

---

## Disclaimer

⚠️ ClinIQ is built for educational and demonstration purposes only. It is not a substitute for professional medical advice, diagnosis, or treatment.

---

Thanks to the Endee community for enabling a streamlined, evaluation-ready workflow.
