//! Getaggte native Aufzählungswerte und die Verzweigung ihrer Fälle.

use super::*;
use crate::native_struct::NativeStructLayout;

type Fall = (Option<Expr>, Option<Expr>, Vec<Stmt>);

impl<'a, 'ctx> Emitter<'a, 'ctx> {
    pub(super) fn aufzaehlungstyp(&self, objekt: &Expr) -> Option<STy> {
        let Expr::Identifier(name) = objekt else { return None };
        if self.slot(name).is_some() { return None; }
        let ty = self.struct_ty(name).ok()?;
        let name = self.struct_name_from_type(&ty)?;
        (!self.layouts.layout(&name)?.varianten.is_empty()).then_some(ty)
    }

    pub(super) fn aufzaehlung_bauen(
        &mut self, ty: &STy, variante: &str, argumente: &[Expr],
    ) -> Result<Place<'ctx>, String> {
        let name = self.struct_name_from_type(ty).ok_or("Aufzählungstyp fehlt")?;
        let layout = self.layouts.layout(&name).ok_or("Aufzählungslayout fehlt")?.clone();
        let (tag, definition) = layout.varianten.iter().enumerate()
            .find(|(_, v)| v.name == variante)
            .ok_or_else(|| format!("Aufzählung '{name}' hat keine Variante '{variante}'"))?;
        if definition.nutzwerte != argumente.len() {
            return Err(format!("Variante '{name}.{variante}' erwartet {} Nutzwerte, bekommen {}",
                definition.nutzwerte, argumente.len()));
        }
        let (ptr, alignment) = self.entry_alloca(ty, "sys.aufzaehlung.wert")?;
        let platz = Place { ptr, alignment, origin: Origin::Fresh };
        let (groesse, _) = self.struct_size_align(ty)?;
        self.builder.build_memset(
            ptr, alignment, self.context.i8_type().const_zero(),
            self.i64t().const_int(groesse, false),
        ).map_err(e)?;
        self.builder.build_store(ptr, self.context.i32_type().const_int(tag as u64, false))
            .map_err(e)?.set_alignment(alignment).map_err(e)?;
        for (i, ausdruck) in argumente.iter().enumerate() {
            let index = definition.erster_nutzwert + i;
            let (feldname, feldtyp) = &layout.fields[index];
            self.strukturfeld_bauen(feldtyp, ausdruck, &platz, layout.offsets[index], feldname)?;
        }
        Ok(platz)
    }

    pub(super) fn pruefe_aufzaehlung(&mut self, wert: &Expr, faelle: &[Fall]) -> Result<(), String> {
        let ty = self.infer(wert).ok_or("prüfe: nativer Werttyp fehlt")?;
        if !matches!(ty, STy::Struct(_)) {
            return Err("prüfe in @nativ erwartet einen Aufzählungswert".into());
        }
        let name = self.struct_name_from_type(&ty).ok_or("prüfe erwartet einen Aufzählungstyp")?;
        let layout = self.layouts.layout(&name).ok_or("Aufzählungslayout fehlt")?.clone();
        if layout.varianten.is_empty() {
            return Err(format!("prüfe in @nativ erwartet eine Aufzählung, erhalten '{name}'"));
        }

        let mut variante_zu_fall = vec![None; layout.varianten.len()];
        let mut fall_zu_variante = Vec::with_capacity(faelle.len());
        let mut standard = None;
        for (i, (muster, _, _)) in faelle.iter().enumerate() {
            let Some(muster) = muster else {
                if standard.replace(i).is_some() || i + 1 != faelle.len() {
                    return Err("prüfe: standard bzw. fall _ gehört einmal ans Ende".into());
                }
                fall_zu_variante.push(None);
                continue;
            };
            let (variante, bindungen) = variantenmuster(muster)?;
            let tag = layout.varianten.iter().position(|v| v.name == variante)
                .ok_or_else(|| format!("prüfe: Aufzählung '{name}' hat keine Variante '{variante}'"))?;
            if variante_zu_fall[tag].replace(i).is_some() {
                return Err(format!("prüfe: Variante '{variante}' kommt mehrfach vor"));
            }
            if layout.varianten[tag].nutzwerte != bindungen.len() {
                return Err(format!("prüfe: Variante '{variante}' erwartet {} Nutzbindungen, bekommen {}",
                    layout.varianten[tag].nutzwerte, bindungen.len()));
            }
            fall_zu_variante.push(Some((tag, bindungen)));
        }
        let abgedeckt = |fall: &Option<usize>| fall.is_some_and(|i| faelle[i].1.is_none());
        let fehlende: Vec<&str> = layout.varianten.iter().zip(&variante_zu_fall)
            .filter_map(|(variante, fall)| (!abgedeckt(fall)).then_some(variante.name.as_str()))
            .collect();
        if standard.is_none() && !fehlende.is_empty() {
            return Err(format!("prüfe für '{name}' ist nicht erschöpfend, fehlende Varianten: {}",
                fehlende.join(", ")));
        }
        let standard_erreichbar = !fehlende.is_empty();

        // Den Ausdruck einmal auswerten. Die Nutzwerte werden nur in dem
        // ausgewaehlten Arm gelesen, bevor der Arm den Eingabewert ändern kann.
        let eingabe = self.struct_place(wert, &ty, "prüfe")?;
        let tagtyp = self.context.i32_type();
        let tag = self.builder.build_load(tagtyp, eingabe.ptr, "sys.aufzaehlung.tag").map_err(e)?;
        tag.as_instruction_value().ok_or("Aufzählungstag ist kein Load")?
            .set_alignment(eingabe.alignment).map_err(e)?;
        let fehler = self.trap_block()?;
        let weiter = self.context.append_basic_block(self.function, "sys.pruefe.weiter");
        let zielbloecke: Vec<_> = faelle.iter().map(|_| {
            self.context.append_basic_block(self.function, "sys.pruefe.fall")
        }).collect();
        let ziele: Vec<_> = variante_zu_fall.iter().enumerate().map(|(tag, fall)| {
            let block = fall.or(standard).map(|i| zielbloecke[i]).unwrap_or(fehler);
            (tagtyp.const_int(tag as u64, false), block)
        }).collect();
        self.builder.build_switch(tag.into_int_value(), fehler, &ziele).map_err(e)?;

        let mut fortsetzung = false;
        for (i, ((_, waechter, body), variante)) in faelle.iter().zip(fall_zu_variante).enumerate() {
            self.builder.position_at_end(zielbloecke[i]);
            let namen = match variante {
                Some((tag, bindungen)) => self.aufzaehlung_binde(&layout, tag, bindungen, &eingabe)?,
                None => Vec::new(),
            };
            if let Some(waechter) = waechter {
                let fallback = standard.ok_or("prüfe: bewachter Fall benötigt standard")?;
                let bedingung = self.expr(waechter, Some(&STy::Bool))?;
                self.expect(&bedingung, &STy::Bool, "prüfe-Wächter")?;
                let arm = self.context.append_basic_block(self.function, "sys.pruefe.waechter");
                self.builder.build_conditional_branch(
                    bedingung.value.into_int_value(), arm, zielbloecke[fallback],
                ).map_err(e)?;
                self.builder.position_at_end(arm);
            }
            let ergebnis = self.block(body);
            for name in namen { self.slots.remove(&name); }
            ergebnis?;
            let offen = crate::native_control_flow::branch_if_open(&self.builder, weiter)?;
            fortsetzung |= offen && (Some(i) != standard || standard_erreichbar);
        }
        self.builder.position_at_end(weiter);
        if !fortsetzung { self.builder.build_unreachable().map_err(e)?; }
        Ok(())
    }

    fn aufzaehlung_binde(
        &mut self, layout: &NativeStructLayout<'ctx>, tag: usize, bindungen: &[Expr], quelle: &Place<'ctx>,
    ) -> Result<Vec<String>, String> {
        let mut namen = Vec::new();
        let variante = &layout.varianten[tag];
        for (i, bindung) in bindungen.iter().enumerate() {
            let Expr::Identifier(name) = bindung else {
                return Err("prüfe: Nutzbindung muss ein Name oder _ sein".into());
            };
            if name == "_" { continue; }
            if self.slot(name).is_some() {
                return Err(format!("prüfe: Nutzbindung '{name}' ist bereits deklariert"));
            }
            let index = variante.erster_nutzwert + i;
            let offset = layout.offsets[index];
            let ty = self.feld_werttyp(&layout.fields[index].1)?;
            self.normaler_speicherzugriff(&ty)?;
            let feld = unsafe {
                self.builder.build_gep(self.context.i8_type(), quelle.ptr,
                    &[self.i64t().const_int(offset, false)], "sys.pruefe.nutzwert")
            }.map_err(e)?;
            let feldalign = field_alignment(quelle.alignment, offset);
            let (ptr, alignment) = self.entry_alloca(&ty, name)?;
            if ty.ist_aggregat() {
                let von = Place { ptr: feld, alignment: feldalign, origin: quelle.origin.clone() };
                let nach = Place { ptr, alignment, origin: Origin::Fresh };
                self.copy_struct(&ty, nach, von)?;
            } else {
                let wert = if let Some((nt, be)) = crate::native_struct::endian_feld(&layout.fields[index].1) {
                    let wert = self.byte_roh(feld, self.i64t().const_zero(), nt, be, None)?;
                    SVal { ty: ty.clone(), value: wert.into() }
                } else {
                    let llvm = ty.llvm(self.context, self.layouts)?;
                    let wert = self.builder.build_load(llvm, feld, name).map_err(e)?;
                    wert.as_instruction_value().ok_or("Nutzwert ist kein Load")?
                        .set_alignment(feldalign).map_err(e)?;
                    SVal { ty: ty.clone(), value: wert }
                };
                self.builder.build_store(ptr, wert.value).map_err(e)?.set_alignment(alignment).map_err(e)?;
            }
            self.slots.insert(name.clone(), Slot { ty, ptr, constant: true });
            namen.push(name.clone());
        }
        Ok(namen)
    }
}

pub(super) fn variantenmuster(muster: &Expr) -> Result<(&str, &[Expr]), String> {
    match muster {
        Expr::Identifier(name) => Ok((name, &[])),
        Expr::FunctionCall { name, type_args, args, .. } if type_args.is_empty() => Ok((name, args)),
        _ => Err("prüfe: Variantenname oder Variante(bindung, ...) erwartet".into()),
    }
}
