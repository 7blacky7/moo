"""P4 A/B/C experiment interface and fail-closed preflight.

This module does not invoke training. A concrete Moo adapter, isolated execution,
GPU telemetry/hard cancellation, and verified final protocol remain mandatory.
It validates the predeclared shape and declared budgets only; it is not a
substitute for a real training runner or proof of GPU resource use.
"""
import hashlib
import json
import math
import re

_SHA256 = re.compile(r"^[0-9a-f]{64}$")


def validate_config(config):
    if config.get("protocol_version") != "moo-p4-measure-v1":
        raise ValueError("unknown protocol")
    for name in ("manifest_sha256", "protocol_sha256", "corpus_sha256"):
        if not isinstance(config.get(name), str) or not _SHA256.fullmatch(config[name]):
            raise ValueError("missing or invalid " + name)
    seeds = config.get("seeds")
    if not isinstance(seeds, list) or len(seeds) != 3 or any(type(s) is not int or s < 0 for s in seeds) or len(set(seeds)) != 3:
        raise ValueError("exactly three different nonnegative integer seeds required")
    if config.get("variants") != ["char", "bpe", "syllable"]:
        raise ValueError("expected ordered A/B/C variants")
    max_run = config.get("gpu_seconds_per_run")
    max_total = config.get("gpu_seconds_total")
    if (type(max_run) not in (int, float) or type(max_total) not in (int, float)
            or not math.isfinite(max_run) or not math.isfinite(max_total)
            or not 0 < max_run <= 600 or not 0 < max_total <= 7200):
        raise ValueError("GPU budgets exceed authorized limits")
    if config.get("split") not in ("train", "val", "test"):
        raise ValueError("invalid split")
    return [(v, s) for v in config["variants"] for s in seeds]


# Separate pin avoids a certificate/hash cycle when this gate is manifested.
# It remains a trusted-local approval boundary, not a digital signature.
from .approval_pin import APPROVED_GATE_SHA256
from .experiment_manifest import canonical, verify
from pathlib import Path


def _checked_manifest(config, prefix):
    path = config.get(prefix + "_manifest_path")
    expected = config.get(prefix + "_manifest_sha256")
    files = config.get(prefix + "_files")
    if not isinstance(path, str) or not isinstance(expected, str) or not _SHA256.fullmatch(expected):
        raise ValueError("missing " + prefix + " manifest")
    if not isinstance(files, list):
        raise ValueError("missing " + prefix + " file coverage")
    raw = Path(path).read_bytes()
    if len(raw) > 1024 * 1024 or hashlib.sha256(raw).hexdigest() != expected:
        raise ValueError(prefix + " manifest hash mismatch")
    manifest = json.loads(raw)
    if canonical(manifest) != raw or verify(manifest, files) != expected:
        raise ValueError(prefix + " manifest invalid")
    return manifest


_ROOT = Path(__file__).resolve().parents[2]
_PRODUCT_RELATIVE = (
    "compiler/Cargo.lock", "compiler/Cargo.toml", "compiler/build.rs",
    "compiler/runtime/moo_silben.c", "compiler/runtime/moo_silben.h",
    "compiler/runtime/moo_char_fixed.c", "compiler/runtime/moo_char_fixed.h",
    "compiler/runtime/moo_runtime.h", "compiler/runtime/tests/test_silben_asan.c",
    "compiler/runtime/tests/test_char_fixed_asan.c",
    "compiler/runtime/data/silben/dehyphn-x-2024-02-28.pat",
    "compiler/src/codegen.rs", "compiler/src/runtime_bindings.rs",
    "beispiele/tests/p4_tokenizer_api.moos",
)
_RUNTIME_MODULES = (
    "harness.py", "experiment_manifest.py", "run_experiment.py", "run_training.py",
    "prepare_run.py", "prepare_training.py", "budget_ledger.py", "wall_budget.py",
    "experiment_ledger.py", "checkpoint_metadata.py", "publish_checkpoint.py",
    "eval_worker.py", "evaluator.py", "moo_encoder.py", "moo_scorer.py",
    "token_bytes.py",
)


def _required_experiment_paths(config):
    required = {str(_ROOT / "skripte/p4_silben" / name) for name in _RUNTIME_MODULES}
    for key in ("manifest_path", "corpus_path", "protocol_path", "encoder_path",
                "train_executable_path", "eval_executable_path", "eval_script_path"):
        value = config.get(key)
        if not isinstance(value, str):
            raise ValueError("missing required experiment path: " + key)
        required.add(value)
    for key in ("train_documents", "tokenizer_paths", "initial_checkpoint_paths", "eval_input_path"):
        mapping = config.get(key)
        if not isinstance(mapping, dict) or not mapping or any(not isinstance(p, str) for p in mapping.values()):
            raise ValueError("missing required experiment mapping: " + key)
        required.update(mapping.values())
    prior = config.get("predecessor")
    if prior is not None:
        if not isinstance(prior, dict) or any(not isinstance(prior.get(k), str) for k in ("config_path", "ledger_path")):
            raise ValueError("invalid predecessor paths")
        required.update((prior["config_path"], prior["ledger_path"]))
    for split in ("val", "test"):
        spec = json.loads(Path(config["eval_input_path"][split]).read_bytes())
        if spec.get("split") != split or not isinstance(spec.get("scorer_path"), str):
            raise ValueError("invalid evaluation input for " + split)
        required.add(spec["scorer_path"])
        documents = spec.get("documents")
        if not isinstance(documents, dict) or not documents or any(not isinstance(p, str) for p in documents.values()):
            raise ValueError("missing evaluation documents for " + split)
        required.update(documents.values())
    return required


def _validate_predecessor(config):
    prior = config.get("predecessor")
    if prior is None:
        return
    if not isinstance(prior, dict):
        raise ValueError("invalid predecessor")
    from . import experiment_ledger
    paths = ("config_path", "ledger_path")
    blobs = {}
    for name in paths:
        path = prior.get(name)
        expected = prior.get(name.replace("_path", "_sha256"))
        if not isinstance(path, str) or not isinstance(expected, str) or not _SHA256.fullmatch(expected):
            raise ValueError("missing predecessor evidence")
        raw = Path(path).read_bytes()
        if len(raw) > 1024 * 1024 or hashlib.sha256(raw).hexdigest() != expected:
            raise ValueError("predecessor evidence drift")
        blobs[name] = raw
    old = json.loads(blobs["config_path"])
    if old.get("ledger_path") != prior["ledger_path"]:
        raise ValueError("predecessor ledger path mismatch")
    lines = blobs["ledger_path"].splitlines()
    if len(lines) != 2 or lines[0] != experiment_ledger.canonical(experiment_ledger.header(old)):
        raise ValueError("unexpected predecessor ledger header or entries")
    item = json.loads(lines[1])
    if (lines[1] != experiment_ledger.canonical(item)
            or item != {"run": ["train", "char", 11], "seconds": 600,
                        "previous": hashlib.sha256(lines[0]).hexdigest()}):
        raise ValueError("unexpected predecessor reservation")
    if (prior.get("reserved_seconds") != 600 or prior.get("preparation_allowance_seconds") != 300
            or prior.get("failed_attempt_wall_upper_seconds") != 5
            or config["gpu_seconds_per_run"] + 5 > 600
            or config["gpu_seconds_total"] + 600 + 300 > 7200):
        raise ValueError("continuation exceeds combined budget")


def device_environment(config):
    if config.get("device_mode") != "vulkan_forced_strict":
        raise ValueError("explicit forced strict Vulkan mode required")
    return {"MOO_KI_GPU": "1", "MOO_KI_GPU_ERZWINGEN": "1", "MOO_KI_GPU_STRIKT": "1"}


def require_ready(config, evidence):
    schedule = validate_config(config)
    if config["split"] != "train":
        raise ValueError("experiment training must use train split")
    if APPROVED_GATE_SHA256 is None:
        raise RuntimeError("gate closed: no reviewed evidence certificate pinned")
    device_environment(config)
    _validate_predecessor(config)
    if not isinstance(evidence, bytes) or len(evidence) > 1024 * 1024:
        raise ValueError("invalid evidence certificate")
    if hashlib.sha256(evidence).hexdigest() != APPROVED_GATE_SHA256:
        raise ValueError("certificate hash not approved")
    def unique(pairs):
        obj = {}
        for key, value in pairs:
            if key in obj:
                raise ValueError("duplicate certificate field")
            obj[key] = value
        return obj
    certificate = json.loads(evidence, object_pairs_hook=unique)
    if not isinstance(certificate, dict) or certificate.get("status") != "approved":
        raise ValueError("certificate not approved")
    # Pin every configuration field, including future additions. This is a
    # trusted-local review anchor, not a signature or runtime isolation.
    canonical_config = json.dumps(config, sort_keys=True, ensure_ascii=False,
                                  separators=(",", ":"), allow_nan=False).encode("utf-8")
    if certificate.get("config_sha256") != hashlib.sha256(canonical_config).hexdigest():
        raise ValueError("certificate config hash mismatch")
    for name in ("manifest_sha256", "protocol_sha256", "corpus_sha256"):
        if certificate.get(name) != config[name]:
            raise ValueError("certificate configuration mismatch")
    product = _checked_manifest(config, "product")
    _checked_manifest(config, "experiment")
    product_required = {str(_ROOT / name) for name in _PRODUCT_RELATIVE}
    if set(config["product_files"]) != product_required:
        raise ValueError("product manifest required coverage mismatch")
    if not _required_experiment_paths(config).issubset(set(config["experiment_files"])):
        raise ValueError("experiment manifest required coverage mismatch")
    for prefix in ("product", "experiment"):
        if certificate.get(prefix + "_manifest_sha256") != config[prefix + "_manifest_sha256"]:
            raise ValueError(prefix + " manifest is not approved")
    if config.get("snapshot_sha256") != config["product_manifest_sha256"]:
        raise ValueError("product snapshot identity mismatch")
    rights = certificate.get("rights_review_sha256")
    if not isinstance(rights, str) or not _SHA256.fullmatch(rights):
        raise ValueError("rights evidence missing")
    reports = certificate.get("test_reports")
    if not isinstance(reports, dict):
        raise ValueError("test evidence missing")
    for stage in ("local", "workspace", "windows"):
        report = reports.get(stage)
        if (not isinstance(report, dict) or type(report.get("rc")) is not int
                or report["rc"] != 0 or type(report.get("checks")) is not int
                or report["checks"] <= 0
                or not isinstance(report.get("snapshot_sha256"), str)
                or not _SHA256.fullmatch(report["snapshot_sha256"])
                or not isinstance(report.get("evidence_sha256"), str)
                or not _SHA256.fullmatch(report["evidence_sha256"])
                or not isinstance(report.get("evidence_path"), str)):
            raise ValueError("missing or invalid evidence for " + stage)
        raw_report = Path(report["evidence_path"]).read_bytes()
        if len(raw_report) > 1024 * 1024 or hashlib.sha256(raw_report).hexdigest() != report["evidence_sha256"]:
            raise ValueError("evidence hash mismatch for " + stage)
        document = json.loads(raw_report, object_pairs_hook=unique)
        if (not isinstance(document, dict) or document.get("stage") != stage
                or document.get("snapshot_sha256") != report["snapshot_sha256"]
                or document.get("rc") != report["rc"] or document.get("checks") != report["checks"]
                or document.get("product_manifest_sha256") != config["product_manifest_sha256"]
                or document.get("product_files") != product["files"]):
            raise ValueError("evidence coverage mismatch for " + stage)
    for name in ("gpu_isolation_evidence_sha256", "budget_enforcement_evidence_sha256"):
        if not isinstance(certificate.get(name), str) or not _SHA256.fullmatch(certificate[name]):
            raise ValueError("missing " + name)
    budget = config.get("train_codepoint_budget")
    if type(budget) is not int or budget <= 0:
        raise ValueError("original-codepoint budget missing")
    return schedule
