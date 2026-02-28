import os
from dotenv import load_dotenv
from datasets import load_dataset
from sentence_transformers import SentenceTransformer
from endee import Endee, Precision


load_dotenv()

# Configuration
INDEX_NAME = "cliniq"
DIMENSION = 384  # all-MiniLM-L6-v2 output size
BATCH_SIZE = 100
MAX_RECORDS_PER_DATASET = 1000


def load_medqa_dataset():
    print("Loading MedQA dataset...")
    ds = load_dataset("GBaker/MedQA-USMLE-4-options", split="train")
    limit = min(MAX_RECORDS_PER_DATASET, len(ds))
    ds = ds.select(range(limit))
    print(f"MedQA loaded with {limit} records")
    return ds


def ingest_records(index, model, dataset, *, id_prefix, source, formatter, question_field, answer_field):
    print(f"Embedding and ingesting {source} records...")
    batch = []
    total = 0

    for i, record in enumerate(dataset):
        question = record[question_field]
        answer = record[answer_field]
        text = formatter(record)
        vector = model.encode(text).tolist()

        batch.append({
            "id": f"{id_prefix}_{i}",
            "vector": vector,
            "meta": {
                "text": text,
                "question": question,
                "answer": answer,
                "source": source,
            },
        })

        if len(batch) == BATCH_SIZE:
            index.upsert(batch)
            total += len(batch)
            print(f"{source}: Ingested {total}/{len(dataset)} records...")
            batch = []

    if batch:
        index.upsert(batch)
        total += len(batch)
        print(f"{source}: Ingested {total}/{len(dataset)} records...")

    print(f"Completed {source} ingestion: {total} records")
    return total

def format_medqa(record):
    return f"Question: {record['question']} Answer: {record['answer']}"


def format_lavita(record):
    return f"Question: {record['input']} Answer: {record['output']}"


def create_endee_client():
    client = Endee()
    client.set_base_url("http://localhost:8080/api/v1")
    return client

def setup_index(client):
    print("Setting up Endee index...")
    try:
        # Delete existing index if exists
        client.delete_index(INDEX_NAME)
        print("Deleted existing index")
    except:
        pass

    client.create_index(
    name=INDEX_NAME,
    dimension=DIMENSION,
    space_type="cosine",
    precision=Precision.FLOAT32
)
    print(f"Created index: {INDEX_NAME}")
    return client.get_index(INDEX_NAME)

def load_dataset_safe(spec):
    print(f"\nLoading {spec['label']} dataset...")
    try:
        ds = load_dataset(
            spec["hf_path"],
            split=spec["split"],
            **spec.get("dataset_kwargs", {})
        )
        limit = min(MAX_RECORDS_PER_DATASET, len(ds))
        ds = ds.select(range(limit))
        print(f"{spec['label']}: Ready with {limit} records")
        return ds
    except Exception as exc:
        print(f"{spec['label']}: Failed to load dataset ({exc}). Skipping.")
        return None


def ingest_dataset(index, model, dataset, spec):
    print(f"Embedding and ingesting {spec['label']} records...")
    batch = []
    total = 0

    for i, record in enumerate(dataset):
        question = record[spec["question_field"]]
        answer = record[spec["answer_field"]]
        text = spec["formatter"](record)
        vector = model.encode(text).tolist()

        batch.append({
            "id": f"{spec['id_prefix']}_{i}",
            "vector": vector,
            "meta": {
                "text": text,
                "question": question,
                "answer": answer,
                "source": spec["source"],
            },
        })

        if len(batch) == BATCH_SIZE:
            index.upsert(batch)
            total += len(batch)
            print(f"{spec['label']}: Ingested {total}/{len(dataset)} records...")
            batch = []

    if batch:
        index.upsert(batch)
        total += len(batch)
        print(f"{spec['label']}: Ingested {total}/{len(dataset)} records...")

    print(f"Completed {spec['label']} ingestion: {total} records")
    return total

def main():
    # Load embedding model
    print("Loading embedding model...")
    model = SentenceTransformer("all-MiniLM-L6-v2")

    # Setup Endee
    client = create_endee_client()
    index = setup_index(client)

    total_ingested = 0
    for spec in DATASET_SPECS:
        dataset = load_dataset_safe(spec)
        if dataset is None:
            continue
        total_ingested += ingest_dataset(index, model, dataset, spec)

    if total_ingested == 0:
        print("No datasets were ingested. Please check dataset configurations.")
    else:
        print(f"Ingestion complete! Total vectors ingested: {total_ingested}")

if __name__ == "__main__":
    main()