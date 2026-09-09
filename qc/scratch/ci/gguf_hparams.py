"""Print the judge GGUF's hyperparameters and derive the per-token KV bytes and the
per-sequence recurrent-state bytes, to check the convergence document's 17,432 B/token
and 52.7 MB fixed against the architecture rather than trusting them.

Usage: python C:/TAPESTRY/qc/scratch/ci/gguf_hparams.py <path.gguf>
"""
import sys

from gguf import GGUFReader

path = sys.argv[1] if len(sys.argv) > 1 else "C:/models/Qwen3.5-9B-emit-v11-Q5_K_M.gguf"
r = GGUFReader(path)

wanted = (
    "general.architecture",
    "general.name",
    "general.file_type",
    "block_count",
    "context_length",
    "embedding_length",
    "attention.head_count",
    "attention.head_count_kv",
    "attention.key_length",
    "attention.value_length",
    "full_attention_interval",
    "ssm.",
    "rope.dimension_count",
    "vocab_size",
    "tokenizer.ggml.model",
)


def scalar(field):
    # A GGUF field stores its payload in field.parts at the indices in field.data.
    try:
        vals = [field.parts[i] for i in field.data]
        out = []
        for v in vals:
            try:
                out.append(v.tolist())
            except AttributeError:
                out.append(v)
        if len(out) == 1:
            v = out[0]
            if isinstance(v, list) and all(isinstance(c, int) for c in v) and field.types and field.types[0].name == "STRING":
                return bytes(v).decode("utf-8", "replace")
            return v
        return out
    except Exception as e:  # noqa: BLE001
        return f"<unreadable: {e}>"


kv = {}
for name, field in r.fields.items():
    if any(w in name for w in wanted):
        val = scalar(field)
        if isinstance(val, list) and len(val) > 64:
            val = f"<array of {len(val)}>"
        kv[name] = val
        print(f"{name} = {val}")

arch = None
for k, v in kv.items():
    if k == "general.architecture":
        arch = v if isinstance(v, str) else bytes(v).decode() if isinstance(v, (bytes, list)) else str(v)
print("\narch:", arch)


def get(suffix, default=None):
    for k, v in kv.items():
        if k.endswith(suffix):
            # scalar fields arrive as one-element lists from the reader; unwrap them
            if isinstance(v, list) and len(v) == 1:
                return v[0]
            return v
    return default


n_layer = get("block_count")
n_head_kv = get("attention.head_count_kv")
head_k = get("attention.key_length")
head_v = get("attention.value_length")
fai = get("full_attention_interval")
d_conv = get("ssm.conv_kernel")
d_state = get("ssm.state_size")
d_inner = get("ssm.inner_size")
n_group = get("ssm.group_count")
ssm_heads = get("ssm.time_step_rank")

print("\n--- derived ---")
print("n_layer", n_layer, "n_head_kv", n_head_kv, "head_k", head_k, "head_v", head_v, "full_attention_interval", fai)
print("ssm: conv_kernel", d_conv, "state_size", d_state, "inner_size", d_inner, "group_count", n_group, "time_step_rank(heads)", ssm_heads)

# A per-layer head_count_kv array means the hybrid is described per layer: 0 kv heads on recurrent layers.
if isinstance(n_head_kv, list):
    attn_layers = sum(1 for h in n_head_kv if h)
    kv_heads_attn = max(n_head_kv)
else:
    attn_layers = (n_layer // fai) if fai else n_layer
    kv_heads_attn = n_head_kv
print("attention layers", attn_layers, "recurrent layers", (n_layer - attn_layers) if n_layer else None, "kv heads on an attention layer", kv_heads_attn)

if attn_layers and kv_heads_attn and head_k and head_v:
    vals_per_tok = attn_layers * kv_heads_attn * (head_k + head_v)
    for name, bpv in (("f16", 2.0), ("q8_0", 34.0 / 32.0), ("q4_0", 18.0 / 32.0)):
        print(f"KV bytes/token at {name}: {vals_per_tok * bpv:,.0f}")

# Recurrent state per sequence: llama.cpp stores the conv state r and the ssm state s in f32.
# For Mamba2/GDN-style layers: s = n_heads * head_dim_k * head_dim_v (or d_inner * d_state), r = (d_conv-1) * conv width.
rec_layers = (n_layer - attn_layers) if (n_layer and attn_layers is not None) else None
if rec_layers and d_conv and d_inner and d_state:
    s_bytes = d_inner * d_state * 4
    r_bytes = (d_conv - 1) * (d_inner + 2 * (n_group or 1) * d_state) * 4
    print(f"per recurrent layer: s={s_bytes:,} B  r={r_bytes:,} B  (mamba-style formula, f32)")
    print(f"recurrent state per sequence (x{rec_layers} layers): {(s_bytes + r_bytes) * rec_layers / 1e6:,.1f} MB")
print("\nreceipt to compare: 17,432 B/token marginal and ~52.7 MB fixed (convergence doc §7.3)")
