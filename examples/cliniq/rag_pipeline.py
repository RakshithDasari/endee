import os
from dotenv import load_dotenv
from sentence_transformers import SentenceTransformer
from endee import Endee
import ollama

load_dotenv()

# Configuration
INDEX_NAME = "cliniq"
TOP_K = 5
SIMILARITY_THRESHOLD = 0.15

# Initialize models once (not on every query)
print("Loading models...")
embedding_model = SentenceTransformer("all-MiniLM-L6-v2")

def get_endee_index():
    client = Endee()
    client.set_base_url("http://localhost:8080/api/v1")
    return client.get_index(INDEX_NAME)

def rewrite_query(query: str) -> str:
    """Step 1: Convert patient language to medical terminology"""
    prompt = f"""Convert the following patient description into formal 
clinical medical terminology. Return only the rewritten query, 
nothing else.

Patient description: {query}
Medical terminology:"""
    response = ollama.chat(
        model="llama3.2:3b",
        messages=[{"role": "user", "content": prompt}]
    )
    rewritten = response["message"]["content"].strip()
    print(f"Rewritten query: {rewritten}")
    return rewritten

def hyde(rewritten_query: str) -> str:
    """Step 2: Generate hypothetical clinical case for better retrieval"""
    prompt = f"""Generate a hypothetical USMLE-style medical exam question 
that would match this clinical scenario. Return only the question, 
nothing else.

Clinical scenario: {rewritten_query}
Hypothetical question:"""
    
    response = ollama.chat(
        model="llama3.2:3b",
        messages=[{"role": "user", "content": prompt}]
    )
    hypothesis = response["message"]["content"].strip()
    print(f"HyDE hypothesis generated")
    return hypothesis

def retrieve(hypothesis: str, index):
    """Step 3: Embed hypothesis and search Endee"""
    vector = embedding_model.encode(hypothesis).tolist()
    results = index.query(vector=vector, top_k=TOP_K)
    return results

def generate_diagnosis(original_query: str, results) -> dict:
    """Step 4: Generate grounded diagnosis from retrieved evidence"""
    
    # Check similarity scores
    if not results or len(results) == 0:
        return {
            "diagnosis": "Insufficient evidence to generate diagnosis.",
            "confidence": "low",
            "sources": []
        }
    
    # Build context from retrieved records
    context = ""
    sources = []
    for i, result in enumerate(results):
        score = result.get("similarity", 0)
        meta = result.get("meta", {}) or {}
        text = meta.get("text", "")
        question = meta.get("question", "")
        answer = meta.get("answer", "")
        source_name = meta.get("source", "Unknown Source")
        
        context += f"\nCase {i+1} (similarity: {score:.2f}):\n{text}\n"
        sources.append({
            "case": i + 1,
            "question": question,
            "answer": answer,
            "similarity": round(score, 2),
            "source": source_name,
        })
    
    # Check if best match is above threshold
    best_score = results[0].get("similarity", 0)
    if best_score < SIMILARITY_THRESHOLD:
        confidence = "low"
    elif best_score < 0.6:
        confidence = "medium"
    else:
        confidence = "high"
    
    # Generate grounded diagnosis
    prompt = f"""You are a clinical decision support system.
    
IMPORTANT: Answer ONLY using the retrieved medical cases below. 
Do not use outside knowledge. If the evidence is insufficient, say so explicitly.

Patient presentation: {original_query}

Retrieved medical evidence:
{context}

Based ONLY on the above evidence, provide:
1. Most likely diagnosis
2. Key supporting findings from the evidence
3. Recommended next steps

If evidence is insufficient, explicitly state that."""
    response = ollama.chat(
        model="llama3.2:3b",
        messages=[{"role": "user", "content": prompt}]
    )
    
    return {
        "diagnosis": response["message"]["content"].strip(),
        "confidence": confidence,
        "sources": sources
    }

def run_pipeline(query: str) -> dict:
    """Full RAG pipeline: rewrite → HyDE → retrieve → generate"""
    print(f"\nProcessing query: {query}")
    
    # Step 1: Query rewriting
    rewritten = rewrite_query(query)
    
    # Step 2: HyDE
    hypothesis = hyde(rewritten)
    
    # Step 3: Retrieve from Endee
    index = get_endee_index()
    results = retrieve(hypothesis, index)
    
    # Step 4: Generate diagnosis
    output = generate_diagnosis(query, results)
    output["rewritten_query"] = rewritten
    output["hypothesis"] = hypothesis
    
    return output

if __name__ == "__main__":
    # Quick test
    test_query = "65 year old male with chest pain radiating to left arm and sweating"
    result = run_pipeline(test_query)
    print("\n--- RESULT ---")
    print(result["diagnosis"])
    print(f"Confidence: {result['confidence']}")
    print(f"Sources used: {len(result['sources'])}")